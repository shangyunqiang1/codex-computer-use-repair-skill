# Codex Capture Compat

[简体中文](README.md) | [English](README_en.md)

面向 Windows 10 的 Codex Computer Use 截图兼容层。项目构建一个本地加载的 x64 `version.dll`，为现有 Computer Use helper 补充缺失的捕获边框接口，并调整截图回调的执行线程。

安装后继续通过原有 Computer Use 功能操作应用，无需单独运行代理程序或新增 MCP 服务。这是独立的兼容实现，不包含官方 helper 的源码。

## 功能

- **捕获边框兼容**：系统缺少 `IGraphicsCaptureSession3` 时，兼容处理 `IsBorderRequired` 属性调用，保留 Windows 默认捕获边框。
- **截图回调派发**：将符合条件的 `FrameArrived` 回调交给 Windows 线程池的 MTA 工作线程，避免在 Windows Graphics Capture（WGC）内部回调中等待图像转换造成阻塞。
- **本地部署与回退**：仅在 helper 同目录安装 DLL 和安装记录，通过路径、文件哈希校验管理卸载。
- **开发验证**：提供 COM 单元测试、真实 WGC 探针、独立测试窗口和可选调用日志。

本项目面向 Windows 10 x64。当前实现对 `codex-computer-use.exe` 和测试程序 `compat_probe.exe` 启用兼容钩子，影响范围限于加载该 DLL 的进程；不修改系统 DLL，不注册全局钩子。

## 下载预编译版本

从 [GitHub Releases](https://github.com/MagicalAstrogy/CodexComputerUseFix/releases) 下载 `CodexCaptureCompat-<版本>-windows-x64.zip`。解压完整目录后，直接按下方使用方法安装，无需配置编译工具链。文件名带 `-trace` 的包用于诊断，日常使用选择普通版。

每个 ZIP 都附有 `.sha256` 校验文件，并包含安装脚本、探针、文档和源码。校验与发布步骤见 [CI 与 Release 指南](capture-compat/docs/ci.md)。

## 从源码构建

在项目根目录打开 PowerShell：

```powershell
Set-Location .\capture-compat
.\build.ps1
.\tests\install_tests.ps1
.\validate.ps1
```

构建需要 MSVC x64 C++ 工具、MASM 和 Windows SDK；实机验证需要已登录、未锁定的 Windows 10 交互桌面。工具链要求及构建选项见[构建与使用指南](capture-compat/README.md)。

构建产物位于 `capture-compat/dist/`：

| 产物 | 用途 |
| --- | --- |
| `version.dll` | 安装到 Computer Use helper 同目录的兼容层 |
| `compat_probe.exe` | 验证边框接口、真实捕获和异步回调 |
| `capture_test_window.exe` | 供 Computer Use 截图测试使用的独立窗口 |

构建和探针验证不会自动安装或更新运行时中的 DLL。

## 使用方法

### 安装

先确认实际使用的 `codex-computer-use.exe` 路径。下面的路径是占位示例，使用前必须替换。

在 `capture-compat` 目录中执行：

```powershell
$helperPath = 'C:\path\to\codex-computer-use.exe'

# 预览安装位置
.\install.ps1 -HelperPath $helperPath -WhatIf

# 退出正在使用此 helper 的进程后安装
.\install.ps1 -HelperPath $helperPath -Action Install
```

重新启动 Computer Use，让 helper 加载新 DLL。安装位置应是 helper 同目录，而不是桌面应用主程序或 Windows 系统目录。

安装脚本新增 `version.dll` 和 `codex-capture-compat.install.json`。它不会修改 helper 可执行文件，也不会替用户终止进程。

### 验证使用效果

打开 `dist/capture_test_window.exe`，通过 Computer Use 选择这个测试窗口并截图。确认画面正确，再验证连续截图与窗口尺寸变化。测试窗口可直接关闭，也会在十分钟后自动退出。

底层探针通过表示兼容层的受测路径正常；实际使用效果还需要在官方 helper 中验证。详细步骤见[验证与排错指南](capture-compat/docs/validation.md)。

### 升级与卸载

升级前先构建新版本，并退出使用目标 DLL 的 helper，然后依次执行：

```powershell
.\install.ps1 -HelperPath $helperPath -Action Uninstall
.\install.ps1 -HelperPath $helperPath -Action Install
```

只需回退时执行卸载命令，再重新启动 Computer Use。脚本拒绝覆盖已有的不同 DLL；卸载也只会删除与安装记录中的路径、DLL 哈希相符的文件。

Codex 更新后，helper 所在目录可能变化，应重新确认实际路径。完整安装规则见[构建与使用指南](capture-compat/README.md)。

## 实现概览

```text
Computer Use helper
  └─ 本地 version.dll
      ├─ 系统 version 导出 → System32 原版 DLL
      └─ RoGetActivationFactory 导入钩子
          └─ WGC 帧池与捕获会话
              ├─ 缺失的边框属性接口 → 兼容接口
              └─ 符合条件的帧事件 → 独立 MTA 工作线程
```

`SetIsBorderRequired` 对应 WinRT/COM 属性调用，并不是 `version.dll` 的导出函数。因此，`version.dll` 在这里承担加载入口和系统导出转发的职责；实际兼容逻辑位于 WGC 对象的接口查询及事件订阅处。

边框兼容只处理原生接口查询返回 `E_NOINTERFACE` 的情况。异步派发还要求帧池不依赖 `DispatcherQueue`、回调支持跨线程调用。事件取消、对象引用和错误处理的细节见[实现说明](capture-compat/docs/implementation.md)。

## 目录结构

```text
capture-compat/
├─ README.md              构建与使用指南
├─ README_en.md           英文构建与使用指南
├─ build.ps1              编译 DLL、探针并运行单元测试
├─ install.ps1            安装、卸载及文件校验
├─ package.ps1            生成发布 ZIP 和 SHA-256 校验文件
├─ validate.ps1           Windows 10 实机回归
├─ src/                   DLL 代理、COM 钩子和回调派发
├─ tests/                 单元测试、捕获探针及测试窗口
├─ docs/                  实现说明、验证与排错
├─ build/                 编译中间产物（生成）
├─ dist/                  可部署产物（生成）
└─ validation/            验证报告和测试图像（生成）
```

`build/`、`dist/` 和 `validation/` 已被工程的 `.gitignore` 排除，源码副本中可能尚不存在这些目录。打包输出位于仓库根目录的 `artifacts/`，同样不进入版本控制。

## CI 与 Release

[Build and release](https://github.com/MagicalAstrogy/CodexComputerUseFix/actions/workflows/build.yml) 工作流在推送 `main`、向 `main` 提交 PR 或手动运行时，构建并测试普通版和 Trace 版，提供保留 14 天的 Actions 下载产物。

推送 `v1.0.0` 这样的版本标签时，两个构建都通过后自动创建 GitHub Release，上传两个 ZIP 及其校验文件。带 `-rc.1` 等后缀的标签发布为预发布版本。CI 运行 COM、回调派发、安装和打包测试；真实截图仍需在 Windows 10 交互桌面验证。详见 [CI 与 Release 指南](capture-compat/docs/ci.md)。

## 适用范围

- 仅提供 x64 构建，面向使用 Windows Graphics Capture 的目标 helper。
- helper 必须允许加载同目录 `version.dll`，并在主 EXE 中直接导入 `RoGetActivationFactory`。
- 兼容行为由实际接口能力决定，不通过修改系统版本或捕获权限来启用。
- Windows 11、其他 helper 构建及不同图形环境仍需各自验证；这不是所有截图故障的通用修复。
- DLL 未签名。异步派发改变了回调线程和错误返回时序，具体约束见[实现说明](capture-compat/docs/implementation.md)。

## 文档

- [构建与使用指南](capture-compat/README.md)
- [实现说明](capture-compat/docs/implementation.md)
- [验证与排错指南](capture-compat/docs/validation.md)
- [CI 与 Release 指南](capture-compat/docs/ci.md)

## 许可证

本项目采用 [WTFPL v2](https://www.wtfpl.net/about/) 许可证，完整文本见 [LICENSE](LICENSE)。
