# Codex Capture Compat · 构建与使用

[简体中文](README.md) | [English](README_en.md)

此目录包含 Windows 10 Computer Use 截图兼容层的源码、构建脚本和测试工具。项目功能介绍见[项目 README](../README.md)，内部机制见[实现说明](docs/implementation.md)。

下文的命令均在本目录执行，helper 路径使用占位示例。

## 构建要求

| 依赖 | 要求 |
| --- | --- |
| Windows | x64 构建环境；完整实机回归面向 Windows 10 |
| PowerShell | 能运行项目脚本并加载 Visual Studio DevShell 模块 |
| Visual Studio 或 Build Tools | 安装 MSVC x64/x86 C++ 工具，支持 C++17 |
| MASM | 提供 `ml64.exe`，用于 x64 导出转发入口 |
| Windows SDK | 包含 C++/WinRT 头文件、`IGraphicsCaptureSession3` 和 Direct3D 11 开发库 |

构建脚本通过 Visual Studio Installer 的 `vswhere.exe` 查找带有 `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` 组件的最新安装，随后进入 x64 host / x64 target 开发环境。通常不需要预先在 Developer Command Prompt 中设置环境变量。

项目直接使用 MSVC 编译和链接，不依赖 CMake、Cargo 或 npm。

## 构建

### 普通构建

```powershell
.\build.ps1
```

脚本依次编译 DLL、测试和探针，生成 `dist/` 文件，然后运行 `core_tests.exe` 和 `dispatch_tests.exe`。成功时输出测试结果及 DLL 的 SHA-256；编译、链接或测试失败时抛出错误。

主要选项是 C++17、`/O2`、静态 CRT `/MT`、`/guard:cf`，并在链接时启用 `/dynamicbase` 和 `/nxcompat`。

### 诊断构建

```powershell
.\build.ps1 -Trace
```

`-Trace` 定义 `CAPTURE_COMPAT_TRACE`，在调用路径上增加诊断钩子和文本日志。它仍使用优化编译，且会覆盖同一组 `build/`、`dist/` 产物；不是一个独立的 Debug 输出目录。

普通构建不写 Trace 文件。切换回普通构建后，如需更新正在使用的 DLL，还要按升级流程重新安装并重启 helper。

### 构建产物

| 路径 | 说明 |
| --- | --- |
| `dist/version.dll` | 部署用代理 DLL |
| `dist/compat_probe.exe` | 可单独运行的 WGC 探针 |
| `dist/capture_test_window.exe` | 简单蓝色窗口，用于官方 Computer Use 验证 |
| `build/core_tests.exe` | COM 边框兼容测试 |
| `build/dispatch_tests.exe` | 回调派发测试 |
| `build/*.obj`、`build/proxy.lib` 等 | 编译和链接中间产物 |

再次构建前关闭正在运行的探针和测试窗口，以免覆盖 EXE 时出现文件占用错误。构建脚本不会自动安装 DLL。

## 测试

```powershell
# 文件安装逻辑测试；使用 validation/ 下的临时文件
.\tests\install_tests.ps1

# 真实 WGC 捕获测试；需要正常交互桌面
.\validate.ps1
```

实机验证会显示探针自己的窗口，并生成 `validation/report.json` 和 `validation/capture.bmp`。基线测试明确预期 Windows 10 缺少边框接口，不能将它直接当作跨 Windows 版本的通用通过标准。

探针参数、输出解释和各测试的覆盖范围见[验证与排错指南](docs/validation.md)。

## 安装与使用

使用预编译 Release 时，保留 ZIP 中的完整目录结构，从解压后的 `capture-compat` 目录执行安装命令即可。发布包已包含 `dist/` 中的三个程序文件，不需要先运行构建脚本。下载与校验说明见 [CI 与 Release 指南](docs/ci.md)。

### 确认目标路径

安装对象是实际运行的 `codex-computer-use.exe`。可以在它运行时只读查看路径：

```powershell
Get-Process -Name codex-computer-use -ErrorAction SilentlyContinue |
    Select-Object Id, Path
```

如果有多个结果，确认当前 Computer Use 使用的是哪一个 runtime；不要根据某个旧安装目录批量部署。进程未运行或权限不足时，列表可能为空或不显示路径。

### 首次安装

```powershell
$helperPath = 'C:\path\to\codex-computer-use.exe'

.\install.ps1 -HelperPath $helperPath -WhatIf

# 退出使用该 helper 的进程后执行
.\install.ps1 -HelperPath $helperPath -Action Install
```

`-Action Install` 是默认值，可以省略。安装后重新启动 Computer Use，继续使用其原有接口。此 DLL 不需要作为独立程序启动。

脚本只向目标目录写入：

```text
codex-computer-use.exe            已有的目标 helper
version.dll                      本项目代理
codex-capture-compat.install.json 安装记录
```

代理必须放在 helper 同目录。安装脚本不查找所有 runtime、不修改系统目录、不修改注册表，也不会终止或重启进程。

### 安装参数

| 参数 | 含义 |
| --- | --- |
| `-HelperPath <路径>` | 必填；必须指向已存在、文件名为 `codex-computer-use.exe` 的文件 |
| `-Action Install` | 安装 `dist/version.dll` |
| `-Action Uninstall` | 按安装记录卸载本项目代理 |
| `-WhatIf` | 预览操作，不复制或删除文件；路径与已有文件校验仍会执行 |

安装记录包含 helper 路径、helper 哈希、DLL 哈希和安装时间。helper 哈希用于记录目标构建；卸载时实际核验的是目标路径和 DLL 哈希。

若已有 DLL 与待安装文件不同，脚本会拒绝覆盖。若 DLL 哈希相同且安装记录存在，则报告已安装。不要通过直接覆盖未知 DLL 来处理此错误。

### 升级

先构建新版本，再退出使用该 DLL 的 helper：

```powershell
.\install.ps1 -HelperPath $helperPath -Action Uninstall
.\install.ps1 -HelperPath $helperPath -Action Install
```

随后重新启动 Computer Use。卸载根据旧安装记录核验已安装文件，不要求它与刚构建出的新 DLL 相同。

### 卸载

```powershell
# 先退出使用该 DLL 的 helper
.\install.ps1 -HelperPath $helperPath -Action Uninstall
```

脚本要求安装记录存在、记录路径匹配，并确认待删除 DLL 的哈希未改变。只删除本项目代理和安装记录，保留 helper。卸载后重新启动 Computer Use，恢复原有加载行为。

如果记录缺失或哈希不符，应先确认文件来源；脚本不会猜测哪个 DLL 可以删除。

## 诊断日志

安装 `-Trace` 构建后，新启动的目标进程可写入：

```text
%TEMP%\codex-capture-compat-<PID>.log
```

这里的 `%TEMP%` 是 helper 进程自己的临时目录。日志记录系统启动后的毫秒数、线程 ID、操作、对象地址和 HRESULT，不保存截图或窗口文本。日志写入失败不会中断原调用。

也可通过只读导出 `CodexCaptureCompatGetStatus` 获取当前进程的统计，接口及字段说明见[实现说明](docs/implementation.md)。

## 部署限制

- 原生支持的边框接口继续由系统处理；缺失接口时的兼容路径保留系统默认捕获边框。
- 更新宿主程序后，应重新确认 helper 路径、导入项及 DLL 加载方式。
- 已加载模块不能靠替换磁盘文件完成热更新，必须重启相应 helper。
- 卸载失败时优先检查文件占用与安装记录，具体错误处理见[验证与排错指南](docs/validation.md)。
