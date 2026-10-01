@echo off
rem ============================================================
rem LilithTimer - MSVC 构建脚本（在 “x64 Native Tools Command Prompt” 中运行）
rem 编译期定制：在 CFLAGS 后追加 /DAPP_NAME=L\"MyTimer\" 等宏，
rem             更换图标直接替换 icon.ico
rem ============================================================
setlocal

set CFLAGS=/nologo /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601
set LIBS=user32.lib gdi32.lib shell32.lib comctl32.lib

cl %CFLAGS% %* lilith_timer.c resource.rc /FeLilithTimer.exe /link /SUBSYSTEM:WINDOWS %LIBS%

if errorlevel 1 (
    echo.
    echo [构建失败] 请确认在 x64 Native Tools Command Prompt 中运行本脚本。
    exit /b 1
)

echo.
echo [构建成功] LilithTimer.exe
endlocal
