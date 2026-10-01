# ============================================================
# LilithTimer - MinGW-w64 构建（可在 Windows 或 Linux 交叉编译）
#
# Windows (MSYS2 / WinLibs):
#   把 CC / WINDRES 改成 gcc / windres，然后执行: mingw32-make
# Linux 交叉编译:
#   sudo apt install mingw-w64 && make
#
# 编译期定制（可选）:
#   make EXTRAFLAGS="-DAPP_NAME=L\"MyTimer\" -DAPP_VERSION=L\"2.0\""
#   更换图标：直接替换 icon.ico
# ============================================================

CC      ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres

TARGET   = LilithTimer.exe
CFLAGS   = -O2 -Wall -Wextra -std=c11 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 $(EXTRAFLAGS)
LDFLAGS  = -mwindows -municode -static
LIBS     = -lgdi32 -luser32 -lshell32 -lcomctl32

all: $(TARGET)

resource.o: resource.rc app.manifest icon.ico
	$(WINDRES) resource.rc -o resource.o

$(TARGET): lilith_timer.c resource.o
	$(CC) $(CFLAGS) -o $@ lilith_timer.c resource.o $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET) resource.o

.PHONY: all clean
