# LilithTimer

一个使用 C 语言编写的轻量级 Win32 计时器。

## 项目背景

本来只是想给学校找一个能作为高考倒计时的软件，但发现市面上同类软件要么需要付费开通 VIP，要么体积臃肿、附带许多无关功能。于是干脆自己用 AI 工具搓了一版轻量级的，无广告、无多余负担，只专注倒计时本身。

## 更新日志

### v1.0.0

- 修复：红蓝通道交换导致的显示颜色异常
- 修复：时区处理错误导致的倒计时偏差
- 修复：用户设置的窗体大小无法保存
- 修复：启动时 SideBySide 配置错误（改用链接器自动生成 manifest）
- 优化：目标时间输入框增加灰色提示与气泡说明，格式错误时自动聚焦并全选

### v1.2.0

- 新增：开机自启 / 定时启动（设置页内开关，默认开启）
  - 每次开机启动：注册表 Run + 启动文件夹快捷方式 + 计划任务 三重通道冗余，
    启动时自动校验，失效通道自动补写；exe 移动位置后也能自愈
  - 每日定时启动：通过计划任务在指定时刻拉起；晚于开机时间时登录通道兜底
- 新增：启动时网络校时（SNTP，多服务器轮询）
  - 网络时间与系统时间偏差 ≥2 分钟时弹出自定义提示框（图标 + 时间对比），
    用户确认后经 UAC 提权执行系统级校时（w32tm /resync，失败回退 SetSystemTime）
  - 网络超时静默失败，不影响正常使用；用户拒绝则本次继续使用本地时间
- 修复：UDP 校时改用 select 等待响应（部分系统上 SO_RCVTIMEO 对 UDP recv 误报超时）

### v1.1.0

- 新增：贴桌面模式拆分为「[原生]贴桌面」与「[兼容]贴桌面」两种
  - [原生]贴桌面：SetParent 挂载到 WorkerW/Progman（原有挂载流）
  - [兼容]贴桌面：顶层分层窗口 + 点击穿透，不挂载，通过 Z-Order 动态守护
    固定在“壁纸之上、桌面图标/普通窗口之下”，并挂接 WinEvent 钩子响应
    Wallpaper Engine / Fences 的窗口重排；同时修复了与之相关的闪烁问题
- 提示：若 [原生]贴桌面 在复杂桌面环境（如 Fences、Wallpaper Engine）下失效，
  请在设置中切换到 [兼容]贴桌面
- 设置界面增强：
  - 字体名称改为下拉框，直接选择系统已安装字体
  - 文字字号 / 倒计时字号 / 内边距 / 行距 均新增滑动条，与输入框双向联动
  - 目标时间改用系统日期时间选择器（日历 + 微调按钮）
- 加固：目标时间解析（支持仅日期输入、尾随校验、大小月/闰年、越界收敛等）
- 工程：MSVC 统一按 UTF-8 编译，源文件统一为 UTF-8 带 BOM

## 功能特性

- 原生 Win32 API，轻量级无依赖
- 支持系统托盘
- 自定义渲染
- 四种显示模式：[原生]贴桌面 / [兼容]贴桌面 / 穿透 / 浮动
- 设置界面：系统字体下拉框、数字滑动条、系统日期时间选择器
- 开机自启 / 每日定时启动：注册表 Run + 启动文件夹 + 计划任务三重冗余，
  启动时自动校验、失效自动补写
- 启动时网络校时（SNTP）：系统时间与网络时间偏差 ≥2 分钟时弹窗询问，
  确认后 UAC 提权执行系统级校时；超时静默失败
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

本项目采用 [MIT](LICENSE) 许可证。

---

# LilithTimer (English)

A lightweight Win32 timer written in C.

## Background

The idea started when I was trying to find a countdown timer for the Gaokao (Chinese college entrance exam) for my school. Most existing software either required a paid VIP subscription or was too bloated with unnecessary features. So I decided to build a lightweight version myself with the help of AI tools. No ads, no bloat—just a simple and effective countdown timer.

## Changelog

### v1.2.0

- Added: Auto-start on boot / scheduled daily start (enabled by default, toggle in Settings)
  - Boot start: triple-redundant channels — registry Run key + Startup-folder shortcut +
    Scheduled Task; verified at every launch and self-healed if broken (survives exe relocation)
  - Daily scheduled start: Scheduled Task triggers at a user-set time of day;
    logon channels act as a fallback when the PC boots later than the set time
- Added: Network time sync on startup (SNTP, multiple servers)
  - Shows a custom prompt (app icon + time comparison) when the system clock deviates
    ≥2 minutes; on confirmation, performs a system-level sync via UAC elevation
    (`w32tm /resync`, falling back to `SetSystemTime`)
  - Timeouts fail silently; declining keeps local time for the session
- Fixed: UDP time sync now waits via `select` (on some systems `SO_RCVTIMEO`
  falsely reports timeouts for UDP recv)

### v1.0.0

- Fixed: Color distortion caused by swapped red/blue channels
- Fixed: Countdown offset due to timezone handling error
- Fixed: Window size set by the user was not saved
- Fixed: SideBySide configuration error at startup (switched to linker-generated manifest)
- Improved: Date input now shows a gray cue banner and a balloon tooltip; invalid input is automatically focused and selected

### v1.1.0

- Added: Desktop mode split into "[Native] Desktop" and "[Compat] Desktop"
  - [Native] Desktop: SetParent mounting onto WorkerW/Progman (original flow)
  - [Compat] Desktop: top-level layered window with click-through, no mounting;
    kept on the desktop via dynamic Z-Order guarding (WinEvent hook); also fixed
    the related flickering caused by fighting with Wallpaper Engine / Fences
- Hint: if [Native] Desktop fails in complex environments (Fences / Wallpaper Engine),
  switch to [Compat] Desktop in settings
- Settings UI improvements:
  - Font name is now a dropdown listing installed system fonts
  - Text size / countdown size / padding / line spacing now have sliders, synced
    bidirectionally with the number boxes
  - Target time now uses the native date-time picker (calendar + spinner)
- Hardened: target time parsing (date-only input, trailing validation,
  days-per-month / leap year, bounds clamping)
- Build: MSVC now compiles sources as UTF-8; all sources normalized to UTF-8 BOM

## Features

- Native Win32 API, lightweight and dependency-free
- System tray support
- Custom rendering
- Four display modes: [Native] Desktop / [Compat] Desktop / Passthrough / Float
- Settings UI: system font dropdown, numeric sliders, native date-time picker
- Auto-start on boot / scheduled daily start: registry Run + Startup folder +
  Scheduled Task triple redundancy, self-verified and self-healed at launch
- Network time sync on startup (SNTP): prompts when system clock deviates ≥2 minutes,
  system-level sync via UAC elevation; silent on timeout
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

This project is licensed under the [MIT](LICENSE) license.
