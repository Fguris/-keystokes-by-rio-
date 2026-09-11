@echo off
setlocal
cl /nologo /std:c++20 /EHsc /W4 rio.cpp user32.lib gdi32.lib ws2_32.lib /link /SUBSYSTEM:WINDOWS /OUT:Rio.exe
if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)
echo.
echo BUILD OK: Rio.exe
endlocal
