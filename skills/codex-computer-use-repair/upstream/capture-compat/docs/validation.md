# 验证与排错

[简体中文](validation.md) | [English](validation_en.md)

使用前先按[构建与使用指南](../README.md)完成构建。以下命令均从 `capture-compat` 目录运行。

## 验证层次

| 层次 | 入口 | 验证内容 |
| --- | --- | --- |
| COM 单元测试 | `build.ps1` 自动运行 `core_tests.exe` | 缺失与原生接口、错误保留、身份、引用计数、空指针及并发查询 |
| 派发单元测试 | `build.ps1` 自动运行 `dispatch_tests.exe` | 独立 MTA、非阻塞提交、合并通知、取消、对象存活、非 agile 回退及失败统计 |
| 安装测试 | `tests/install_tests.ps1` | WhatIf、复制哈希、重复安装、卸载和已有未知 DLL 保护 |
| WGC 实机测试 | `validate.ps1` | 真实帧、像素内容、线程模式、SoftwareBitmap 转换及关闭清理 |
| 官方接口验证 | 使用已安装补丁的 Computer Use | 实际 helper 能否返回正确截图 |

安装测试在 `validation/install-fixture-<随机标识>/` 创建模拟文件，不启动其中的假 helper，也不修改真实运行时。测试完成后目录会保留。

## 完整实机回归

```powershell
.\validate.ps1
```

需要正常登录且未锁定的 Windows 10 桌面，以及可用的 Direct3D 11 图形环境。探针只创建并截取自己的蓝色测试窗口，结束后关闭窗口。

脚本运行以下八项检查：

1. 无本地代理时，边框接口返回预期的 `E_NOINTERFACE`。
2. 非目标 EXE 加载代理时，不启用捕获钩子。
3. 收到真实事件后取帧，并检查蓝色像素。
4. MTA 捕获线程不处理窗口消息时仍能收到帧。
5. STA 捕获线程不处理窗口消息时仍能收到帧。
6. 启动捕获、清空已有帧后再订阅事件。
7. SoftwareBitmap 转换在被派发的回调内完成。
8. 帧池关闭时清理仍注册的异步回调。

无消息循环测试只停用**捕获线程**的消息处理；测试窗口始终在自己的 UI 线程处理消息。

第一项有明确的 Windows 10 缺失接口预期。若系统原生支持该接口，基线可能因“未出现预期错误”而失败，这与代理破坏截图是不同情况。

### 输出文件

| 文件 | 内容 |
| --- | --- |
| `validation/report.json` | 时间、系统版本、当前 DLL 哈希、各项输出和退出码 |
| `validation/capture.bmp` | 探针测试窗口的真实捕获图像 |
| `validation/baseline/` | 不放置代理 DLL 的基线程序 |
| `validation/unrelated/` | 验证进程名筛选的程序及代理副本 |

回归遇到首个失败就停止；报告保留已执行项，因此仅有报告文件不代表全部八项通过。重新运行会覆盖当前报告和图像。上述目录是生成产物，默认不进入版本控制。

## 单独运行探针

### 标准捕获

```powershell
New-Item -ItemType Directory -Path .\validation -Force | Out-Null
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --output .\validation\capture.bmp
```

### 回调内图像转换与关闭清理

```powershell
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --software-bitmap
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --software-bitmap --close-subscribed
```

### 线程与订阅顺序

```powershell
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --no-pump
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --sta --no-pump
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --late-subscribe
```

### 参数参考

| 参数 | 作用 |
| --- | --- |
| `--expect-shim` | 要求代理及导入钩子已加载，边框属性兼容成功 |
| `--capture` | 启动真实捕获，等待帧并验证像素 |
| `--expect-deferred` | 要求存在派发调用，收尾后无存活包装器及派发错误；与捕获参数一起使用 |
| `--output <路径>` | 将真实帧写为 BMP；需使用 `--capture`，父目录应已存在 |
| `--sta` | 捕获线程使用 STA；默认 MTA |
| `--no-pump` | 捕获线程等待时不主动处理窗口消息 |
| `--late-subscribe` | 先启动捕获，等待并清空帧，再注册事件 |
| `--recreate` | 注册事件后，以原尺寸和格式重新创建帧池资源 |
| `--software-bitmap` | 在回调中启动 SoftwareBitmap 转换并观察完成状态 |
| `--close-subscribed` | 不主动移除事件 token，用关闭帧池路径验证清理 |
| `--poll-only` | 允许在没有事件通知时轮询取帧，用于对照；此结果不能证明事件路径正常 |

不带 `--expect-shim` 时，探针会要求出现原生 Windows 10 缺失接口错误。因此不要将直接运行已带代理的 `dist/compat_probe.exe` 当作默认自检；使用上面的标准命令，或由 `validate.ps1` 准备基线目录。

退出码：`0` 为通过，`1` 为验证失败，`2` 为未知或不完整的命令行参数。

### 关键输出

- `proxy_loaded=1 iat_hooks=1`：探针加载了代理并装上导入钩子。
- `SetIsBorderRequired_hr=0x00000000`：属性调用成功；原生基线则预期 `0x80004002`。
- `frame_events`、`callback_thread`：事件计数和实际执行线程。
- `expected_color_pixels`：捕获结果中匹配测试蓝色的像素数；不足图像面积四分之一时失败。
- `software_bitmap=status_in_callback_1`：转换在回调中已经 Completed；`0` 表示 Started。
- `live_handlers=0`、`dispatch_errors=0`：收尾阶段未发现存活包装器或记录到的派发错误。

探针对帧等待、转换状态观察和引用释放检查设置了时间上限。它不是任意驱动或 COM 调用的强制终止器；外层原生调用本身阻塞时，仍可能需要结束测试进程。

## 在官方 helper 中验证

安装并重新启动相应 helper 后：

1. 打开 `dist/capture_test_window.exe`。
2. 使用 Computer Use 枚举并选中这个窗口。
3. 请求截图，检查窗口标题和蓝色测试内容。
4. 再请求连续截图，并改变窗口尺寸确认返回当前画面。
5. 分别验证不带截图的辅助功能读取，最后关闭测试窗口。

底层探针没有复刻所有官方调用路径，不能用其通过结果替代这一步。记录当前 helper 和 DLL 的 SHA-256、操作是否成功及调用耗时，便于不同构建之间比较。

## 日志定位

构建并安装诊断版本：

```powershell
.\build.ps1 -Trace
```

安装方法与普通版本相同，仍需先退出旧 helper，再卸载旧 DLL、安装新 DLL 并重新启动。

在 helper 的临时目录查找 `codex-capture-compat-<PID>.log`。调用顺序可以区分：

| 观察 | 可以确认的范围 |
| --- | --- |
| 没有预期日志 | 可能加载了普通构建、未触发钩子，或日志路径不可写；不能只据此判定未加载 |
| `StartCapture.leave` 成功 | 捕获启动调用返回成功 |
| `FrameArrived.add.deferred` | 注册使用了异步包装器 |
| `FrameArrived.add.native_fallback` | 包装器创建失败，使用原生注册路径；HRESULT 有助于判断原因 |
| `Invoke.enter` 后有 `TryGetNextFrame.frame` | 原回调已开始，并取到有效帧 |
| `Invoke.enter` 长时间没有 `Invoke.leave` | 阻塞位置在原回调内部，不能归因于事件完全未触发 |
| `Invoke.leave` 返回错误 | 原回调执行失败，需结合 HRESULT 与派发统计判断 |

日志时间及额外调用可能影响调度。比较性能时分别记录普通构建与诊断构建，不把一次诊断运行的耗时视作保证。

## 常见问题

| 现象 | 检查与处理 |
| --- | --- |
| 找不到 `vswhere.exe` / MSVC | 安装或补全 Visual Studio Installer、C++ x64/x86 工具组件 |
| 缺少 `IGraphicsCaptureSession3` 或 WinRT 头文件 | 检查 Windows SDK 与 C++/WinRT 开发头文件是否完整 |
| 构建时目标 EXE 被占用 | 关闭本项目探针或测试窗口，再重新构建 |
| 已有 `version.dll`，安装被拒绝 | 确认其来源；本项目旧版本使用脚本卸载后升级，不覆盖未知 DLL |
| 安装记录不存在或哈希不符 | 检查文件是否被替换、路径是否选错；保留文件供核对 |
| 安装后仍报边框接口缺失 | 检查是否安装在实际 helper 同目录、是否已重启、加载方式是否仍兼容 |
| 探针在创建捕获目标时失败 | 检查是否处于正常交互桌面；隔离、锁屏或不可捕获目标会影响此步骤 |
| 仍然截图超时 | 使用日志区分启动失败、未收到事件、取帧失败和回调内部阻塞 |
| 探针通过、官方调用失败 | 核验实际加载的 DLL 与构建哈希，再检查官方路径中的转换与窗口状态 |
| 卸载提示文件正在使用 | 先退出所有使用该目录中 DLL 的 helper，再运行卸载；无需终止无关程序 |
