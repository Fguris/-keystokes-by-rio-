#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")

// ============================================================
// RIO C++20 controller
// Controls the RioFPS Fabric bridge over localhost only.
// No process injection, memory editing or DLL loading is used.
// ============================================================

struct Color { BYTE r{}, g{}, b{}; };

static constexpr Color C_BG      {16, 18, 22};
static constexpr Color C_PANEL   {23, 26, 32};
static constexpr Color C_ROW     {29, 33, 40};
static constexpr Color C_ROW_ON  {34, 44, 54};
static constexpr Color C_TEXT    {230, 234, 240};
static constexpr Color C_MUTED   {145, 153, 163};
static constexpr Color C_BLUE    {98, 191, 255};
static constexpr Color C_PURPLE  {169, 112, 255};
static constexpr Color C_RED     {235, 90, 90};
static constexpr Color C_WHITE   {245, 245, 245};
static constexpr Color C_GL_BLUE {90, 160, 255};
static constexpr Color C_GREEN   {90, 220, 135};

static COLORREF rgb(Color c) { return RGB(c.r, c.g, c.b); }

static bool inRect(int x, int y, const RECT& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static void fillRectColor(HDC dc, const RECT& r, Color c) {
    HBRUSH brush = CreateSolidBrush(rgb(c));
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

static void outlineRect(HDC dc, const RECT& r, Color c, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, rgb(c));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void drawTextSimple(
    HDC dc,
    int x,
    int y,
    const std::wstring& value,
    Color color,
    int size = 15,
    int weight = FW_NORMAL
) {
    HFONT font = CreateFontW(
        -size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, rgb(color));
    TextOutW(dc, x, y, value.c_str(), static_cast<int>(value.size()));
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

static std::wstring keyName(int vk) {
    if (vk == 0) return L"-";

    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC) << 16;
    if (vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
        vk == VK_INSERT || vk == VK_DELETE) {
        scan |= 1 << 24;
    }

    wchar_t buffer[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan), buffer, 64) > 0) {
        return buffer;
    }
    return L"VK " + std::to_wstring(vk);
}

// ----------------------------- Module state -----------------------------

enum class TGMode { COMBA, KRIT };
enum class GLMode { VISIBLE, ALL };
enum class GLColor { RED, WHITE, BLUE };

struct TGState {
    std::atomic_bool enabled{false};
    std::atomic<TGMode> mode{TGMode::COMBA};
    bool expanded{false};
    int bind{0};
};

struct GLState {
    std::atomic_bool enabled{false};
    std::atomic<GLMode> mode{GLMode::VISIBLE};
    std::atomic<int> width{2};
    std::atomic<GLColor> color{GLColor::BLUE};
    bool expanded{false};
    int bind{0};
};

static TGState g_tg;
static GLState g_gl;

// ----------------------------- Bridge client ----------------------------

static constexpr unsigned short BRIDGE_PORT = 38765;

static std::atomic_bool g_running{true};
static std::atomic_bool g_connected{false};
static SOCKET g_socket = INVALID_SOCKET;
static CRITICAL_SECTION g_socketLock;
static std::thread g_networkThread;

static std::string statePacket() {
    std::string packet;
    packet.reserve(160);

    packet += "HELLO RIOCPP 2\n";
    packet += std::string("TG_ENABLED ") + (g_tg.enabled.load() ? "1\n" : "0\n");
    packet += std::string("TG_MODE ") +
        (g_tg.mode.load() == TGMode::COMBA ? "COMBA\n" : "KRIT\n");
    packet += std::string("GL_ENABLED ") + (g_gl.enabled.load() ? "1\n" : "0\n");
    packet += std::string("GL_MODE ") +
        (g_gl.mode.load() == GLMode::VISIBLE ? "VISIBLE\n" : "ALL\n");
    packet += "GL_WIDTH " + std::to_string(g_gl.width.load()) + "\n";

    switch (g_gl.color.load()) {
        case GLColor::RED:   packet += "GL_COLOR RED\n"; break;
        case GLColor::WHITE: packet += "GL_COLOR WHITE\n"; break;
        case GLColor::BLUE:  packet += "GL_COLOR BLUE\n"; break;
    }

    packet += "PING\n";
    return packet;
}

static bool sendAll(SOCKET s, const char* data, int length) {
    int sentTotal = 0;
    while (sentTotal < length) {
        int result = send(s, data + sentTotal, length - sentTotal, 0);
        if (result == SOCKET_ERROR || result == 0) {
            return false;
        }
        sentTotal += result;
    }
    return true;
}

static void closeCurrentSocket() {
    EnterCriticalSection(&g_socketLock);
    if (g_socket != INVALID_SOCKET) {
        shutdown(g_socket, SD_BOTH);
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
    LeaveCriticalSection(&g_socketLock);
    g_connected = false;
}

static bool sendDirect(const std::string& data) {
    bool ok = false;
    EnterCriticalSection(&g_socketLock);
    if (g_socket != INVALID_SOCKET) {
        ok = sendAll(g_socket, data.data(), static_cast<int>(data.size()));
    }
    LeaveCriticalSection(&g_socketLock);
    return ok;
}

static SOCKET connectBridge() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(BRIDGE_PORT);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    BOOL noDelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    return s;
}

static void networkLoop() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return;
    }

    while (g_running.load()) {
        if (!g_connected.load()) {
            SOCKET s = connectBridge();
            if (s != INVALID_SOCKET) {
                EnterCriticalSection(&g_socketLock);
                g_socket = s;
                LeaveCriticalSection(&g_socketLock);
                g_connected = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                continue;
            }
        }

        const std::string packet = statePacket();
        if (!sendDirect(packet)) {
            closeCurrentSocket();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            continue;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    closeCurrentSocket();
    WSACleanup();
}

// ------------------------------- GUI -----------------------------------

struct GuiState {
    bool visible{true};
    bool dragging{false};
    bool draggingWidth{false};
    int dragOffsetX{};
    int dragOffsetY{};
    int waitingBind{0}; // 0 none, 1 TG, 2 GL, 3 UnHook
    int unhookBind{0};
};

struct Layout {
    RECT header{};
    RECT tgRow{};
    RECT tgMode{};
    RECT glRow{};
    RECT glMode{};
    RECT glWidth{};
    RECT redButton{};
    RECT whiteButton{};
    RECT blueButton{};
    RECT unhookRow{};
    RECT panel{};
};

static GuiState g_gui;
static HWND g_window = nullptr;

static Layout makeLayout() {
    Layout l{};
    const int x = 0;
    const int w = 320;
    int y = 0;

    l.header = {x, y, x + w, y + 46};
    y += 46;

    l.tgRow = {x, y, x + w, y + 40};
    y += 40;
    if (g_tg.expanded) {
        l.tgMode = {x + 12, y, x + w - 12, y + 44};
        y += 44;
    }

    l.glRow = {x, y, x + w, y + 40};
    y += 40;
    if (g_gl.expanded) {
        l.glMode = {x + 12, y, x + w - 12, y + 44};
        y += 44;
        l.glWidth = {x + 12, y, x + w - 12, y + 48};
        y += 48;
        l.redButton = {x + 12, y + 6, x + 72, y + 34};
        l.whiteButton = {x + 82, y + 6, x + 150, y + 34};
        l.blueButton = {x + 160, y + 6, x + 224, y + 34};
        y += 42;
    }

    l.unhookRow = {x, y, x + w, y + 40};
    y += 40;
    l.panel = {0, 0, w, y};
    return l;
}

static void resizeWindowToLayout() {
    if (!g_window) return;
    Layout l = makeLayout();
    SetWindowPos(
        g_window, nullptr, 0, 0,
        l.panel.right - l.panel.left,
        l.panel.bottom - l.panel.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    );
}

static Color glColorUi() {
    switch (g_gl.color.load()) {
        case GLColor::RED: return C_RED;
        case GLColor::WHITE: return C_WHITE;
        case GLColor::BLUE: return C_GL_BLUE;
    }
    return C_WHITE;
}

static void drawModuleRow(
    HDC dc, const RECT& r, const wchar_t* label,
    bool enabled, int bind, bool waiting
) {
    fillRectColor(dc, r, enabled ? C_ROW_ON : C_ROW);
    if (enabled) {
        RECT accent{r.left, r.top, r.left + 3, r.bottom};
        fillRectColor(dc, accent, C_BLUE);
    }

    drawTextSimple(dc, r.left + 12, r.top + 11, label,
        enabled ? C_TEXT : C_MUTED, 15, FW_SEMIBOLD);

    const std::wstring bindText = waiting
        ? L"[ ... ]"
        : L"[ " + keyName(bind) + L" ]";
    drawTextSimple(dc, r.right - 88, r.top + 11, bindText,
        waiting ? C_BLUE : C_MUTED, 13);
}

static void drawModePair(
    HDC dc, const RECT& r,
    const wchar_t* leftText, const wchar_t* rightText,
    bool leftActive
) {
    drawTextSimple(dc, r.left, r.top + 14, L"MODE", C_MUTED, 13);

    const int start = r.left + 72;
    const int mid = (start + r.right) / 2;
    RECT left{start, r.top + 7, mid - 4, r.bottom - 7};
    RECT right{mid + 4, r.top + 7, r.right, r.bottom - 7};

    fillRectColor(dc, left, leftActive ? C_ROW_ON : C_PANEL);
    fillRectColor(dc, right, leftActive ? C_PANEL : C_ROW_ON);
    outlineRect(dc, left, leftActive ? C_BLUE : C_MUTED);
    outlineRect(dc, right, leftActive ? C_MUTED : C_BLUE);

    drawTextSimple(dc, left.left + 10, left.top + 6, leftText,
        leftActive ? C_BLUE : C_MUTED, 12, FW_SEMIBOLD);
    drawTextSimple(dc, right.left + 10, right.top + 6, rightText,
        leftActive ? C_MUTED : C_BLUE, 12, FW_SEMIBOLD);
}

static void drawWidthSlider(HDC dc, const RECT& r) {
    drawTextSimple(dc, r.left, r.top + 16, L"WIDTH", C_MUTED, 13);

    const int x0 = r.left + 78;
    const int x1 = r.right - 8;
    const int y = r.top + 27;

    HPEN basePen = CreatePen(PS_SOLID, 3, rgb(C_MUTED));
    HGDIOBJ oldPen = SelectObject(dc, basePen);
    MoveToEx(dc, x0, y, nullptr);
    LineTo(dc, x1, y);
    SelectObject(dc, oldPen);
    DeleteObject(basePen);

    const int widthValue = g_gl.width.load();
    const float t = static_cast<float>(widthValue - 1) / 29.0f;
    const int knobX = x0 + static_cast<int>((x1 - x0) * t);

    HPEN activePen = CreatePen(PS_SOLID, 4, rgb(C_BLUE));
    oldPen = SelectObject(dc, activePen);
    MoveToEx(dc, x0, y, nullptr);
    LineTo(dc, knobX, y);
    SelectObject(dc, oldPen);
    DeleteObject(activePen);

    HBRUSH knob = CreateSolidBrush(rgb(C_BLUE));
    HGDIOBJ oldBrush = SelectObject(dc, knob);
    Ellipse(dc, knobX - 5, y - 5, knobX + 5, y + 5);
    SelectObject(dc, oldBrush);
    DeleteObject(knob);

    drawTextSimple(dc, r.right - 50, r.top + 2,
        std::to_wstring(widthValue) + L" px", C_TEXT, 12);
}

static void drawColorButton(
    HDC dc, const RECT& r, const wchar_t* label,
    Color color, bool active
) {
    fillRectColor(dc, r, active ? C_ROW_ON : C_PANEL);
    outlineRect(dc, r, active ? color : C_MUTED);
    drawTextSimple(dc, r.left + 8, r.top + 6, label,
        active ? color : C_MUTED, 11, FW_SEMIBOLD);
}

static void render(HDC dc, const RECT& client) {
    fillRectColor(dc, client, C_PANEL);
    const Layout l = makeLayout();

    fillRectColor(dc, l.header, C_BG);
    drawTextSimple(dc, 12, 10, L"RIO", C_PURPLE, 21, FW_BOLD);

    const bool connected = g_connected.load();
    drawTextSimple(dc, 178, 14,
        connected ? L"BRIDGE: ONLINE" : L"BRIDGE: OFFLINE",
        connected ? C_GREEN : C_RED, 11, FW_SEMIBOLD);

    drawModuleRow(dc, l.tgRow, L"TG",
        g_tg.enabled.load(), g_tg.bind, g_gui.waitingBind == 1);

    if (g_tg.expanded) {
        drawModePair(dc, l.tgMode, L"COMBA", L"KRIT",
            g_tg.mode.load() == TGMode::COMBA);
    }

    drawModuleRow(dc, l.glRow, L"GL",
        g_gl.enabled.load(), g_gl.bind, g_gui.waitingBind == 2);

    if (g_gl.expanded) {
        drawModePair(dc, l.glMode, L"VISIBLE", L"ALL",
            g_gl.mode.load() == GLMode::VISIBLE);
        drawWidthSlider(dc, l.glWidth);

        drawTextSimple(dc, l.redButton.left, l.redButton.top - 15,
            L"COLOR", C_MUTED, 12);
        drawColorButton(dc, l.redButton, L"RED", C_RED,
            g_gl.color.load() == GLColor::RED);
        drawColorButton(dc, l.whiteButton, L"WHITE", C_WHITE,
            g_gl.color.load() == GLColor::WHITE);
        drawColorButton(dc, l.blueButton, L"BLUE", C_GL_BLUE,
            g_gl.color.load() == GLColor::BLUE);
    }

    fillRectColor(dc, l.unhookRow, C_ROW);
    outlineRect(dc, l.unhookRow, C_RED);
    drawTextSimple(dc, 12, l.unhookRow.top + 11, L"UnHook", C_RED, 15, FW_SEMIBOLD);

    const std::wstring unhookBind = g_gui.waitingBind == 3
        ? L"[ ... ]"
        : L"[ " + keyName(g_gui.unhookBind) + L" ]";
    drawTextSimple(dc, l.unhookRow.right - 88, l.unhookRow.top + 11,
        unhookBind, g_gui.waitingBind == 3 ? C_BLUE : C_MUTED, 13);
}

// ------------------------------- Actions --------------------------------

// Previous states for edge-triggered global hotkeys.
static bool g_prevMenu = false;
static bool g_prevTG = false;
static bool g_prevGL = false;
static bool g_prevUnhook = false;

static void requestUnhook() {
    if (!g_running.exchange(false)) return;

    g_tg.enabled = false;
    g_gl.enabled = false;
    sendDirect("TG_ENABLED 0\nGL_ENABLED 0\nUNHOOK\n");

    closeCurrentSocket();
    if (g_networkThread.joinable()) {
        g_networkThread.join();
    }

    if (g_window) {
        PostMessageW(g_window, WM_CLOSE, 0, 0);
    }
}

static void assignKey(int vk) {
    if (vk == VK_ESCAPE) {
        g_gui.waitingBind = 0;
        return;
    }

    if (vk == VK_DELETE || vk == VK_BACK) {
        if (g_gui.waitingBind == 1) g_tg.bind = 0;
        if (g_gui.waitingBind == 2) g_gl.bind = 0;
        if (g_gui.waitingBind == 3) g_gui.unhookBind = 0;
        g_gui.waitingBind = 0;
        return;
    }

    // Right Shift remains reserved for showing/hiding RIO.
    if (vk == VK_RSHIFT) return;

    if (g_gui.waitingBind == 1) {
        g_tg.bind = vk;
        g_prevTG = true;
    }
    if (g_gui.waitingBind == 2) {
        g_gl.bind = vk;
        g_prevGL = true;
    }
    if (g_gui.waitingBind == 3) {
        g_gui.unhookBind = vk;
        g_prevUnhook = true;
    }
    g_gui.waitingBind = 0;
}

static void leftDown(int mx, int my) {
    const Layout l = makeLayout();

    if (inRect(mx, my, l.header)) {
        POINT cursor{};
        GetCursorPos(&cursor);
        RECT wr{};
        GetWindowRect(g_window, &wr);
        g_gui.dragging = true;
        g_gui.dragOffsetX = cursor.x - wr.left;
        g_gui.dragOffsetY = cursor.y - wr.top;
        SetCapture(g_window);
        return;
    }

    if (inRect(mx, my, l.tgRow)) {
        g_tg.enabled = !g_tg.enabled.load();
        return;
    }

    if (g_tg.expanded && inRect(mx, my, l.tgMode)) {
        const int start = l.tgMode.left + 72;
        const int mid = (start + l.tgMode.right) / 2;
        g_tg.mode = (mx < mid) ? TGMode::COMBA : TGMode::KRIT;
        return;
    }

    if (inRect(mx, my, l.glRow)) {
        g_gl.enabled = !g_gl.enabled.load();
        return;
    }

    if (g_gl.expanded && inRect(mx, my, l.glMode)) {
        const int start = l.glMode.left + 72;
        const int mid = (start + l.glMode.right) / 2;
        g_gl.mode = (mx < mid) ? GLMode::VISIBLE : GLMode::ALL;
        return;
    }

    if (g_gl.expanded && inRect(mx, my, l.glWidth)) {
        g_gui.draggingWidth = true;
        SetCapture(g_window);
        return;
    }

    if (g_gl.expanded && inRect(mx, my, l.redButton)) {
        g_gl.color = GLColor::RED;
        return;
    }
    if (g_gl.expanded && inRect(mx, my, l.whiteButton)) {
        g_gl.color = GLColor::WHITE;
        return;
    }
    if (g_gl.expanded && inRect(mx, my, l.blueButton)) {
        g_gl.color = GLColor::BLUE;
        return;
    }

    if (inRect(mx, my, l.unhookRow)) {
        requestUnhook();
    }
}

static void rightDown(int mx, int my) {
    const Layout l = makeLayout();
    if (inRect(mx, my, l.tgRow)) {
        g_tg.expanded = !g_tg.expanded;
        resizeWindowToLayout();
        return;
    }
    if (inRect(mx, my, l.glRow)) {
        g_gl.expanded = !g_gl.expanded;
        resizeWindowToLayout();
    }
}

static void middleDown(int mx, int my) {
    const Layout l = makeLayout();
    if (inRect(mx, my, l.tgRow)) g_gui.waitingBind = 1;
    else if (inRect(mx, my, l.glRow)) g_gui.waitingBind = 2;
    else if (inRect(mx, my, l.unhookRow)) g_gui.waitingBind = 3;
}

static void mouseMove() {
    POINT cursor{};
    GetCursorPos(&cursor);

    if (g_gui.dragging) {
        SetWindowPos(
            g_window, nullptr,
            cursor.x - g_gui.dragOffsetX,
            cursor.y - g_gui.dragOffsetY,
            0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        );
    }

    if (g_gui.draggingWidth) {
        POINT clientPoint = cursor;
        ScreenToClient(g_window, &clientPoint);
        Layout l = makeLayout();
        const int x0 = l.glWidth.left + 78;
        const int x1 = l.glWidth.right - 8;
        const float t = std::clamp(
            static_cast<float>(clientPoint.x - x0) /
            static_cast<float>(std::max(1, x1 - x0)),
            0.0f, 1.0f
        );
        g_gl.width = 1 + static_cast<int>(std::round(t * 29.0f));
    }
}

// ---------------------------- Global bind polling -----------------------

static bool keyDown(int vk) {
    return vk != 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool edgeFor(int vk, bool& previous) {
    const bool now = keyDown(vk);
    const bool edge = now && !previous;
    previous = now;
    return edge;
}

static void pollGlobalBinds() {
    if (g_gui.waitingBind != 0) return;

    if (edgeFor(VK_RSHIFT, g_prevMenu)) {
        g_gui.visible = !g_gui.visible;
        ShowWindow(g_window, g_gui.visible ? SW_SHOW : SW_HIDE);
        if (g_gui.visible) {
            SetForegroundWindow(g_window);
        }
    }

    if (g_tg.bind != 0 && edgeFor(g_tg.bind, g_prevTG)) {
        g_tg.enabled = !g_tg.enabled.load();
    } else if (g_tg.bind == 0) {
        g_prevTG = false;
    }

    if (g_gl.bind != 0 && edgeFor(g_gl.bind, g_prevGL)) {
        g_gl.enabled = !g_gl.enabled.load();
    } else if (g_gl.bind == 0) {
        g_prevGL = false;
    }

    if (g_gui.unhookBind != 0 && edgeFor(g_gui.unhookBind, g_prevUnhook)) {
        requestUnhook();
    } else if (g_gui.unhookBind == 0) {
        g_prevUnhook = false;
    }
}

// ------------------------------- Win32 ----------------------------------

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER:
            pollGlobalBinds();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_KEYDOWN:
            if (g_gui.waitingBind != 0 && (lParam & (1 << 30)) == 0) {
                assignKey(static_cast<int>(wParam));
            }
            return 0;

        case WM_LBUTTONDOWN:
            leftDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_RBUTTONDOWN:
            rightDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MBUTTONDOWN:
            middleDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
            mouseMove();
            return 0;

        case WM_LBUTTONUP:
            g_gui.dragging = false;
            g_gui.draggingWidth = false;
            ReleaseCapture();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
            render(mem, client);
            BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            if (g_running.load()) {
                requestUnhook();
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            g_window = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    InitializeCriticalSection(&g_socketLock);

    const wchar_t* className = L"RioControllerClass";
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassW(&wc)) {
        DeleteCriticalSection(&g_socketLock);
        return 1;
    }

    Layout initial = makeLayout();
    g_window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        className,
        L"RIO",
        WS_POPUP,
        80, 80,
        initial.panel.right,
        initial.panel.bottom,
        nullptr, nullptr, instance, nullptr
    );

    if (!g_window) {
        DeleteCriticalSection(&g_socketLock);
        return 2;
    }

    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);
    SetTimer(g_window, 1, 16, nullptr);

    g_networkThread = std::thread(networkLoop);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_running.exchange(false)) {
        g_tg.enabled = false;
        g_gl.enabled = false;
        sendDirect("TG_ENABLED 0\nGL_ENABLED 0\nUNHOOK\n");
        closeCurrentSocket();
        if (g_networkThread.joinable()) g_networkThread.join();
    }

    DeleteCriticalSection(&g_socketLock);
    return static_cast<int>(message.wParam);
}
