# 实现说明

[简体中文](implementation.md) | [English](implementation_en.md)

本文描述当前源码中的加载、COM 兼容和事件派发机制。使用步骤见[构建与使用指南](../README.md)。

## 模块分工

| 文件 | 职责 |
| --- | --- |
| [proxy.cpp](../src/proxy.cpp) | 进程筛选、系统 version DLL 的延迟加载、诊断导出 |
| [version.def](../src/version.def) | 系统导出名称、序号与代理入口映射 |
| [version.asm](../src/version.asm) | 保留 x64 调用参数并尾跳转到系统函数 |
| [compat.cpp](../src/compat.cpp) | 导入表与 COM 虚表钩子、边框接口、订阅管理 |
| [compat.h](../src/compat.h) | 兼容层入口及只读状态结构 |
| [frame_dispatch.cpp](../src/frame_dispatch.cpp) | 可跨线程的事件包装器、MTA 派发与取消 |
| [frame_dispatch.h](../src/frame_dispatch.h) | 包装器创建、取消及统计接口 |

## 1. DLL 加载与系统导出转发

helper 加载同目录的 `version.dll` 后，代理的 `DllMain` 检查当前 EXE 的文件名。只有 `codex-computer-use.exe` 和 `compat_probe.exe` 会初始化捕获钩子。其他进程即使加载代理，也只使用 version 导出转发功能。

代理保留系统 version 的 17 个具名导出及对应序号，并额外提供序号 100 的 `CodexCaptureCompatGetStatus`。首次调用版本查询函数时，`InitOnceExecuteOnce` 从系统目录加载原版 DLL，用 `GetProcAddress` 解析并缓存入口。

汇编转发器保存整数参数寄存器 `RCX/RDX/R8/R9` 和 `XMM0–XMM3`，调用解析器，再恢复寄存器与栈并尾跳转。栈上传入的参数仍按原来的调用约定交给系统函数；包括未在项目中声明完整原型的导出，也不需要猜测其参数列表。

如果系统 DLL 或所需入口无法解析，转发器调用 `RaiseFailFastException`，不会返回伪造的成功结果。

## 2. 从激活工厂连接到捕获会话

初始化在主 EXE 的 PE 导入表中查找具名导入 `RoGetActivationFactory`，保存其真实地址并替换相应 IAT 项。这一阶段不创建 COM 对象、不启动工作线程、不进行文件日志或额外 DLL 加载，以避免在 loader lock 下执行这些操作。

激活工厂钩子先调用原函数。只有成功返回 `Windows.Graphics.Capture.Direct3D11CaptureFramePool` 的工厂时，才进一步挂接：

```text
RoGetActivationFactory
  → IDirect3D11CaptureFramePoolStatics.Create
    或 IDirect3D11CaptureFramePoolStatics2.CreateFreeThreaded
  → IDirect3D11CaptureFramePool.CreateCaptureSession
  → IGraphicsCaptureSession.QueryInterface
```

代码同时处理直接请求 statics 接口，以及先取得其他工厂接口再查询 statics 的路径。它使用 Windows SDK 定义的 COM ABI，不依赖某个 helper 构建的机器码偏移。

主要虚表位置如下，索引从 0 开始：

| 接口 | 方法 | 索引 |
| --- | --- | --- |
| `IUnknown` | `QueryInterface` | 0 |
| 帧池 statics / statics2 | `Create` / `CreateFreeThreaded` | 6 |
| `IDirect3D11CaptureFramePool` | `add_FrameArrived` | 8 |
| `IDirect3D11CaptureFramePool` | `remove_FrameArrived` | 9 |
| `IDirect3D11CaptureFramePool` | `CreateCaptureSession` | 10 |
| 帧池的 `IClosable` | `Close` | 6 |

`PatchSlot` 按虚表槽地址保存原入口，以 SRWLOCK 保护记录，用 `VirtualProtect` 临时改变指针所在页的保护，再原子替换指针并恢复保护。同一槽重复安装会检查已有入口是否一致。

记录表最多保存 64 个槽。相关函数所在模块会被 pin 到进程退出，防止虚表仍被使用时钩子代码已卸载。因此本实现没有进程内热卸载接口。

## 3. 边框属性兼容

`IsBorderRequired` 属于 WinRT 接口 `IGraphicsCaptureSession3`，不是 Win32 DLL 导出。缺失接口时，失败通常发生在属性调用前的 `QueryInterface`。

`SessionQuery` 先执行原生查询，只在以下条件成立时创建 `BorderFallback`：

- 查询的 IID 是 `IGraphicsCaptureSession3`。
- 原生返回值为 `E_NOINTERFACE`。
- 调用者提供了有效输出指针。

兼容对象的 getter 返回 `true`，setter 返回 `S_OK` 并增加统计，不尝试调用系统不支持的 setter。捕获边框仍由系统默认行为决定。

兼容对象持有真实 session 的引用。其 `IUnknown` 查询和其他接口查询交给原 session，保留真实对象身份；释放最后一个兼容对象引用时释放 owner。已经支持的原生接口、其他 IID 和 `E_ACCESSDENIED` 等错误继续按原生结果返回。

## 4. 为什么需要更换回调执行线程

自由线程帧池在 WGC 内部工作线程上触发 `FrameArrived`。如果调用方在此回调内同步等待 `SoftwareBitmap.CreateCopyFromSurfaceAsync`，受影响的捕获路径可能形成等待循环：回调等待转换完成，而转换需要 WGC 内部回调先返回才能继续。

派发层将原回调提交到 Windows 线程池，使 WGC 的事件调用可以及时返回。线程池任务通过 `RoInitialize(RO_INIT_MULTITHREADED)` 建立 MTA 环境，再执行原回调。

```text
WGC 内部线程                     线程池 MTA
    │
    ├─ 包装器 Invoke
    ├─ 提交工作 ────────────────→ 原回调 Invoke
    └─ 返回 S_OK                    ├─ 获取真实帧
                                    ├─ 等待图像转换
                                    └─ 完成调用并释放引用
```

两侧可以并发执行；包装器不会同步等待工作线程结束。

### 启用条件

实现不直接比较 Windows 版本号。创建会话时会绕过已安装的兼容 QI，再查询真实的 Session3 支持情况。发现原生 `E_NOINTERFACE` 后设置进程级 `deferred_capture` 标记，并挂接事件及关闭路径。该标记持续至进程退出。

每次订阅还要满足以下条件：

1. handler 和 token 输出指针有效。
2. 帧池的 `get_DispatcherQueue` 成功且返回空值。
3. 原 handler 可查询到 `IAgileObject`，允许跨线程调用。

条件不满足或包装器创建失败时，代码保留原生事件订阅路径。若已创建包装器后无法分配订阅记录，则返回 `E_OUTOFMEMORY`；原生注册失败则保留其 HRESULT。

当前 WGC 事件参数通常为空。如果事件带有无法查询到 `IAgileObject` 的参数，包装器会同步调用原 handler，避免把不支持跨线程的参数直接交给线程池。

### 队列、引用与取消

每个包装器以 `busy_` 限制最多一个排队或运行中的任务。忙碌期间的重复通知被合并；该机制面向“有帧可取”的事件，不保证逐帧回调交付。

队列项持有包装器、sender 和可选事件参数的引用；包装器持有原 handler。工作完成后释放这些引用，因此事件取消不会使正在执行的回调访问已释放的对象。

订阅表按帧池指针和 `EventRegistrationToken` 记录包装器：

- 原生 `remove_FrameArrived` 成功后，移除记录并设置取消标记。
- `Close` 先取消记录、尝试解除原生订阅，然后执行原生关闭。
- 尚未开始执行原 handler 的任务在检查到取消标记时跳过调用。
- 已经执行的回调继续完成，不强行终止线程。
- 从订阅表释放 COM 对象时不持有表锁，以允许析构路径重入。

## 5. 错误返回与诊断

异步派发的 `Invoke` 返回成功表示工作已经提交，或通知被合并／取消，不等于原回调已执行成功。原回调的最终 HRESULT 无法同步返回给 WGC。队列提交失败可以立即返回错误；工作线程初始化失败和原回调失败计入统计。

`CodexCaptureCompatGetStatus` 的签名是：

```cpp
BOOL WINAPI CodexCaptureCompatGetStatus(
    capture_compat::Status* status,
    DWORD size);
```

调用者必须与当前 [compat.h](../src/compat.h) 一起编译，传入有效指针和 `sizeof(capture_compat::Status)`。空指针或结构尺寸不匹配返回 `FALSE`。此导出读取的是**调用进程自身**的状态；在另一个进程加载 DLL，不能得到目标 helper 的统计。

| 字段 | 含义 |
| --- | --- |
| `size` | 当前状态结构的大小 |
| `iat_hooks` | 已安装的 EXE 导入钩子数量 |
| `factory_calls` | 激活工厂钩子收到的调用数量 |
| `pool_hooks` | 成功挂接会话创建方法的帧池次数 |
| `session_hooks` | 成功挂接 QI 的 session 次数 |
| `border_interfaces` | 创建的兼容边框接口对象数量 |
| `border_noops` | 兼容边框 setter 调用次数 |
| `hook_errors` | 记录到的钩子安装或保护恢复错误 |
| `live_handlers` | 当前存活的异步包装器数量 |
| `dispatched_calls` | 在工作线程上开始执行的原回调数量 |
| `dispatch_errors` | 记录到的线程初始化、任务提交或原回调失败数量 |

计数是进程级统计。它们逐项读取，不是多个线程在同一时刻的事务快照。正常工作中的 `live_handlers` 可以非零；取消订阅、关闭并等待已开始的回调完成后，才能据此检查对象是否释放。

`-Trace` 还会跟踪启动、事件订阅、原回调进入／离开及取帧。普通构建不写这些 Trace 文件，但仍可能通过 `OutputDebugStringW` 向调试器输出兼容提示和钩子错误。

## 6. 维护边界

- 当前仅修改主 EXE 已解析的具名导入。动态解析、仅按序号导入或移入其他模块的实现不在此入口覆盖范围内。
- Hook 不修改目标 EXE 的磁盘内容，也不为受限捕获请求授予额外权限。
- 修改系统 version 导出时，须同步维护 `version.def`、汇编入口数量和 `proxy.cpp` 中的名称顺序。
- 修改 `Status` 时须同步重编译诊断调用者。
- 修改派发逻辑时，应同时覆盖取消、在途引用、非 agile 回退和真实 SoftwareBitmap 转换测试。
- 本项目不实现逐帧录制队列，也不把所有 HRESULT 或超时统一转换为成功。
