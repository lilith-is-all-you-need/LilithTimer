# LilithTimer

一个使用 C 语言编写的轻量级 Win32 计时器。

## 功能特性

- 原生 Win32 API，轻量级无依赖
- 支持系统托盘
- 自定义渲染
- 通过 `lilith_timer.ini` 配置文件进行设置
- 支持 Visual Studio 和 Makefile 两种构建方式

## 构建方式

### 使用 Visual Studio

1. 使用 Visual Studio 2022 (17.10+) 打开 `LilithTimer.slnx`。
2. 选择 `Release` 和 `x64`。
3. 生成解决方案。

### 使用 MSVC 命令行

```bat
cd src
build_msvc.bat
```

### 使用 Makefile (MinGW / MSYS2)

```bash
cd src
make
```

## 使用方法

1. 运行 `LilithTimer.exe`。
2. 程序启动后会自动最小化到系统托盘。
3. 右键点击托盘图标可以访问设置或退出程序。
4. 配置保存在 `lilith_timer.ini` 中。

## 项目结构

```
LilithTimer/
├── LilithTimer.slnx
├── LilithTimer/          # Visual Studio 工程文件
│   ├── LilithTimer.vcxproj
│   ├── LilithTimer.rc
│   └── resource.h
└── src/                  # 源代码
    ├── main.c
    ├── window.c
    ├── render.c
    ├── tray.c
    ├── settings.c
    ├── config.c
    ├── lilith_timer.h
    ├── resource.rc
    ├── app.manifest
    ├── lilith.ico
    ├── Makefile
    └── build_msvc.bat
```

## 许可证

*(如果你想开源，可以在这里添加许可证，例如 MIT)*

---

# LilithTimer (English)

A lightweight Win32 timer written in C.

## Features

- Native Win32 API, lightweight and dependency-free
- System tray support
- Custom rendering
- Configurable via `lilith_timer.ini`
- Supports both Visual Studio and Makefile builds

## Build

### Visual Studio

1. Open `LilithTimer.slnx` in Visual Studio 2022 (17.10+).
2. Select `Release` and `x64`.
3. Build the solution.

### MSVC Command Line

```bat
cd src
build_msvc.bat
```

### Makefile (MinGW / MSYS2)

```bash
cd src
make
```

## Usage

1. Run `LilithTimer.exe`.
2. The application will start minimized in the system tray.
3. Right-click the tray icon to access settings or exit.
4. Configuration is stored in `lilith_timer.ini`.

## Project Structure

```
LilithTimer/
├── LilithTimer.slnx
├── LilithTimer/          # Visual Studio project files
│   ├── LilithTimer.vcxproj
│   ├── LilithTimer.rc
│   └── resource.h
└── src/                  # Source code
    ├── main.c
    ├── window.c
    ├── render.c
    ├── tray.c
    ├── settings.c
    ├── config.c
    ├── lilith_timer.h
    ├── resource.rc
    ├── app.manifest
    ├── lilith.ico
    ├── Makefile
    └── build_msvc.bat
```

## License

*(Add a license if you want, e.g. MIT)*
