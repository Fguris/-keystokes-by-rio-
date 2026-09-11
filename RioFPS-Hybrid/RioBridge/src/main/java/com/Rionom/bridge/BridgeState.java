package com.Rionom.bridge;

import net.minecraft.util.math.MathHelper;

public final class BridgeState {
    public enum TGMode { COMBA, KRIT }
    public enum GLMode { VISIBLE, ALL }
    public enum GLColor { RED, WHITE, BLUE }

    private static volatile boolean tgEnabled;
    private static volatile TGMode tgMode = TGMode.COMBA;

    private static volatile boolean glEnabled;
    private static volatile GLMode glMode = GLMode.VISIBLE;
    private static volatile int glWidth = 2;
    private static volatile GLColor glColor = GLColor.BLUE;

    private BridgeState() {}

    public static boolean tgEnabled() { return tgEnabled; }
    public static TGMode tgMode() { return tgMode; }
    public static boolean glEnabled() { return glEnabled; }
    public static GLMode glMode() { return glMode; }
    public static int glWidth() { return glWidth; }
    public static GLColor glColor() { return glColor; }

    public static void setTGEnabled(boolean value) { tgEnabled = value; }
    public static void setTGMode(TGMode value) { tgMode = value; }
    public static void setGLEnabled(boolean value) { glEnabled = value; }
    public static void setGLMode(GLMode value) { glMode = value; }
    public static void setGLWidth(int value) { glWidth = MathHelper.clamp(value, 1, 30); }
    public static void setGLColor(GLColor value) { glColor = value; }

    public static void disableAll() {
        tgEnabled = false;
        glEnabled = false;
    }
}
