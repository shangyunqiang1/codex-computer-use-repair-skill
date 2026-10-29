# Implementation

[简体中文](implementation.md) | [English](implementation_en.md)

This guide describes the loading, COM compatibility, and event dispatch mechanisms in the current source. See the [build and usage guide](../README_en.md) for operating instructions.

## Modules

| File | Responsibility |
| --- | --- |
| [proxy.cpp](../src/proxy.cpp) | Process filtering, lazy loading of the system version DLL, and the diagnostic export |
| [version.def](../src/version.def) | System export names, ordinals, and proxy entry mappings |
| [version.asm](../src/version.asm) | Preserves x64 call arguments and tail-jumps to system functions |
| [compat.cpp](../src/compat.cpp) | Import and COM vtable hooks, the border interface, and subscription management |
| [compat.h](../src/compat.h) | Compatibility entry points and the read-only status structure |
| [frame_dispatch.cpp](../src/frame_dispatch.cpp) | Event wrappers that support cross-thread calls, MTA dispatch, and cancellation |
| [frame_dispatch.h](../src/frame_dispatch.h) | Wrapper creation, cancellation, and statistics interfaces |

## 1. DLL loading and system export forwarding

When the helper loads the local `version.dll`, the proxy's `DllMain` checks the current executable's filename. Capture hooks are initialized only for `codex-computer-use.exe` and `compat_probe.exe`. Other processes that load the proxy receive only the version export forwarding behavior.

The proxy preserves the system version DLL's 17 named exports and their ordinals, and adds `CodexCaptureCompatGetStatus` at ordinal 100. On the first version function call, `InitOnceExecuteOnce` loads the original DLL from the system directory, resolves its entries with `GetProcAddress`, and caches them.

The assembly forwarder saves the integer argument registers `RCX/RDX/R8/R9` and `XMM0–XMM3`, calls the resolver, restores the registers and stack, and tail-jumps to the resolved function. Stack arguments reach the system function under the original calling convention. This also supports exports whose full prototypes are not declared in the project without guessing their parameter lists.

If the system DLL or a required entry cannot be resolved, the forwarder calls `RaiseFailFastException` instead of returning a fabricated success result.

## 2. Connecting activation factories to capture sessions

Initialization searches the main EXE's PE import table for the named import `RoGetActivationFactory`, saves its real address, and replaces the corresponding IAT entry. This phase avoids creating COM objects, starting workers, writing file logs, or loading additional DLLs under the loader lock.

The activation factory hook calls the original function first. It installs further hooks only after a successful factory result for `Windows.Graphics.Capture.Direct3D11CaptureFramePool`:

```text
RoGetActivationFactory
  → IDirect3D11CaptureFramePoolStatics.Create
    or IDirect3D11CaptureFramePoolStatics2.CreateFreeThreaded
  → IDirect3D11CaptureFramePool.CreateCaptureSession
  → IGraphicsCaptureSession.QueryInterface
```

The code handles both direct requests for statics interfaces and requests for another factory interface followed by a statics query. It uses the COM ABI defined by the Windows SDK and does not depend on machine-code offsets in a particular helper build.

The main vtable positions are listed below, with zero-based indices:

| Interface | Method | Index |
| --- | --- | --- |
| `IUnknown` | `QueryInterface` | 0 |
| Frame pool statics / statics2 | `Create` / `CreateFreeThreaded` | 6 |
| `IDirect3D11CaptureFramePool` | `add_FrameArrived` | 8 |
| `IDirect3D11CaptureFramePool` | `remove_FrameArrived` | 9 |
| `IDirect3D11CaptureFramePool` | `CreateCaptureSession` | 10 |
| Frame pool's `IClosable` | `Close` | 6 |

`PatchSlot` records the original entry by vtable slot address. An SRWLOCK protects the records; `VirtualProtect` temporarily changes the pointer page's protection before an atomic pointer replacement, then restores the protection. Reinstalling the same slot checks that its current entry matches the expected hook.

The table holds at most 64 slots. Modules containing the relevant functions are pinned until process exit so that hook code remains available while the patched vtables are in use. There is no interface for unloading the hooks from a running process.

## 3. Border property compatibility

`IsBorderRequired` belongs to the WinRT interface `IGraphicsCaptureSession3`; it is not a Win32 DLL export. When the interface is missing, the failure usually occurs in `QueryInterface` before the property operation itself.

`SessionQuery` performs the native query first and creates a `BorderFallback` only when:

- The requested IID is `IGraphicsCaptureSession3`.
- The native result is `E_NOINTERFACE`.
- The caller supplies a valid output pointer.

The fallback getter returns `true`. Its setter returns `S_OK` and increments a counter without attempting to call an unsupported system setter. The capture border remains governed by the system's default behavior.

The fallback holds a reference to the real session. Queries for `IUnknown` and other interfaces are delegated to that session, preserving its object identity. Releasing the last fallback reference releases its owner. Supported native interfaces, other IIDs, and errors such as `E_ACCESSDENIED` retain their native results.

## 4. Why dispatch callbacks on another thread

A free-threaded frame pool raises `FrameArrived` on an internal WGC worker. If the caller synchronously waits for `SoftwareBitmap.CreateCopyFromSurfaceAsync` inside that callback, an affected capture path can enter a waiting cycle: the callback waits for conversion, while conversion needs the internal WGC callback to return before it can proceed.

The dispatch layer submits the original callback to the Windows thread pool, allowing WGC's event call to return promptly. The worker calls `RoInitialize(RO_INIT_MULTITHREADED)` to establish an MTA environment, then invokes the original callback.

```text
Internal WGC thread                  Thread pool MTA
    │
    ├─ Wrapper Invoke
    ├─ Submit work ─────────────────→ Original callback Invoke
    └─ Return S_OK                      ├─ Get a real frame
                                        ├─ Wait for image conversion
                                        └─ Finish and release references
```

Both sides can run concurrently. The wrapper does not synchronously wait for the worker to finish.

### Activation conditions

The implementation does not compare Windows version numbers directly. During session creation, it bypasses any installed compatibility QI hook to query native Session3 support. A native `E_NOINTERFACE` sets the process-wide `deferred_capture` flag and installs event and close hooks. That flag remains set until process exit.

Each subscription must also satisfy these conditions:

1. The handler and token output pointers are valid.
2. The frame pool's `get_DispatcherQueue` succeeds and returns null.
3. The original handler exposes `IAgileObject`, allowing calls across threads.

If these conditions are not met or wrapper creation fails, the native event subscription path is preserved. If a wrapper has been created but the subscription record cannot be allocated, the call returns `E_OUTOFMEMORY`. A native registration failure retains its HRESULT.

Current WGC event arguments are normally null. If an event supplies arguments that do not expose `IAgileObject`, the wrapper calls the original handler synchronously rather than passing apartment-bound arguments directly to the thread pool.

### Queueing, references, and cancellation

Each wrapper uses `busy_` to allow at most one queued or running task. Repeated notifications while busy are coalesced. This is a frame-availability notification mechanism and does not guarantee one callback for every frame.

A queued work item holds references to the wrapper, sender, and optional event arguments. The wrapper holds the original handler. These references are released when work finishes, so cancellation does not invalidate objects held by a running callback.

The subscription registry associates wrappers with a frame pool pointer and an `EventRegistrationToken`:

- Successful native `remove_FrameArrived` calls remove the record and set the cancellation flag.
- `Close` cancels the records, attempts native unsubscription, then calls the native close method.
- Tasks that observe cancellation before invoking the original handler skip the call.
- Callbacks already executing are allowed to finish; threads are not forcibly terminated.
- COM objects removed from the registry are released outside its lock to allow destructor reentry.

## 5. Error results and diagnostics

A successful asynchronous `Invoke` means work was submitted, or a notification was coalesced or cancelled. It does not mean the original callback completed successfully. The original callback's final HRESULT cannot be returned synchronously to WGC. Submission failures can return an immediate error; worker initialization and callback failures are counted in the statistics.

`CodexCaptureCompatGetStatus` has this signature:

```cpp
BOOL WINAPI CodexCaptureCompatGetStatus(
    capture_compat::Status* status,
    DWORD size);
```

Compile callers against the current [compat.h](../src/compat.h), pass a valid pointer, and use `sizeof(capture_compat::Status)`. A null pointer or mismatched structure size returns `FALSE`. The export reads **the calling process's own state**. Loading the DLL in a different process does not expose the target helper's statistics.

| Field | Meaning |
| --- | --- |
| `size` | Size of the current status structure |
| `iat_hooks` | Number of installed EXE import hooks |
| `factory_calls` | Calls received by the activation factory hook |
| `pool_hooks` | Successful frame pool session-creation hook operations |
| `session_hooks` | Successful session QI hook operations |
| `border_interfaces` | Fallback border interface objects created |
| `border_noops` | Fallback border setter calls |
| `hook_errors` | Recorded hook installation or protection restoration errors |
| `live_handlers` | Currently live asynchronous wrappers |
| `dispatched_calls` | Original callback invocations started on worker threads |
| `dispatch_errors` | Recorded worker initialization, submission, or callback failures |

Statistics are process-wide and are read field by field, not as a transactional snapshot across threads. A nonzero `live_handlers` value is normal during operation. To check for released objects, first unsubscribe, close the pool, and wait for running callbacks to finish.

`-Trace` also tracks capture startup, event subscriptions, original callback entry and exit, and frame retrieval. Regular builds do not write these Trace files, but may still emit compatibility notices and hook errors to a debugger through `OutputDebugStringW`.

## 6. Maintenance boundaries

- Only resolved named imports in the main EXE are modified. Dynamic resolution, ordinal-only imports, and implementations moved into other modules are outside this entry point's coverage.
- Hooks do not change the target EXE on disk or grant extra permission to restricted capture requests.
- Changes to system version exports must keep `version.def`, the assembly entry count, and the name order in `proxy.cpp` synchronized.
- Changes to `Status` require diagnostic callers to be rebuilt.
- Dispatch changes should cover cancellation, in-flight references, non-agile fallback, and real SoftwareBitmap conversion tests.
- The project does not implement a per-frame recording queue or turn all HRESULT failures and timeouts into success.
