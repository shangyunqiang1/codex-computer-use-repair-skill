# Validation and troubleshooting

[简体中文](validation.md) | [English](validation_en.md)

Build the project using the [build and usage guide](../README_en.md) before running these checks. All commands below run from the `capture-compat` directory.

## Validation layers

| Layer | Entry point | Coverage |
| --- | --- | --- |
| COM unit tests | `build.ps1` automatically runs `core_tests.exe` | Missing and native interfaces, preserved errors, identity, reference counts, null pointers, and concurrent queries |
| Dispatch unit tests | `build.ps1` automatically runs `dispatch_tests.exe` | Separate MTA execution, nonblocking submission, notification coalescing, cancellation, object lifetime, non-agile fallback, and failure statistics |
| Installation tests | `tests/install_tests.ps1` | WhatIf, copy hashes, repeated installation, removal, and protection of existing unknown DLLs |
| Live WGC tests | `validate.ps1` | Real frames, pixel content, threading modes, SoftwareBitmap conversion, and close cleanup |
| Official interface validation | Computer Use with the compatibility DLL installed | Whether the actual helper returns correct screenshots |

Installation tests create simulated files under `validation/install-fixture-<random-ID>/`. They do not execute the fake helper or modify a real runtime. The fixture directory is retained after testing.

## Full live regression suite

```powershell
.\validate.ps1
```

The suite requires a logged-in, unlocked Windows 10 desktop and a working Direct3D 11 graphics environment. Each probe creates and captures its own blue test window, then closes it.

The script runs eight checks:

1. Without a local proxy, the border interface returns the expected `E_NOINTERFACE`.
2. A non-target EXE loading the proxy does not enable capture hooks.
3. A real event allows frame retrieval, and the captured image contains the expected blue pixels.
4. The MTA capture thread receives frames without pumping window messages.
5. The STA capture thread receives frames without pumping window messages.
6. Event subscription works after capture has started and existing frames have been drained.
7. SoftwareBitmap conversion completes inside the dispatched callback.
8. Closing a frame pool cleans up asynchronous callbacks that are still subscribed.

The no-pump tests disable message processing only on the **capture thread**. The test window always processes messages on its own UI thread.

The first test explicitly expects the interface to be missing on Windows 10. If the system supports it natively, the baseline may fail because the expected error did not occur. Such a result does not by itself indicate that the proxy broke capture.

### Output files

| File | Contents |
| --- | --- |
| `validation/report.json` | Timestamp, OS version, current DLL hash, individual test output, and exit codes |
| `validation/capture.bmp` | A real capture of the probe's test window |
| `validation/baseline/` | Baseline program without a proxy DLL |
| `validation/unrelated/` | Program and proxy copy used to test executable-name filtering |

The suite stops on the first failure and retains results for the tests it ran. The presence of a report alone does not mean all eight tests passed. A new run overwrites the current report and image. These directories contain generated files and are excluded from version control by default.

## Running the probe directly

### Standard capture

```powershell
New-Item -ItemType Directory -Path .\validation -Force | Out-Null
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --output .\validation\capture.bmp
```

### Conversion inside the callback and close cleanup

```powershell
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --software-bitmap
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --software-bitmap --close-subscribed
```

### Threading and subscription order

```powershell
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --no-pump
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --sta --no-pump
.\dist\compat_probe.exe --expect-shim --capture --expect-deferred --late-subscribe
```

### Parameter reference

| Parameter | Effect |
| --- | --- |
| `--expect-shim` | Requires the proxy and import hook to be loaded and border property compatibility to succeed |
| `--capture` | Starts real capture, waits for a frame, and validates its pixels |
| `--expect-deferred` | Requires dispatched calls, no live wrappers after cleanup, and no dispatch errors; use with capture options |
| `--output <path>` | Saves a real frame as BMP; requires `--capture` and an existing parent directory |
| `--sta` | Uses STA for the capture thread; the default is MTA |
| `--no-pump` | Does not actively pump window messages while the capture thread waits |
| `--late-subscribe` | Starts capture, waits and drains frames, then subscribes to events |
| `--recreate` | Recreates frame pool resources with the original dimensions and format after subscription |
| `--software-bitmap` | Starts SoftwareBitmap conversion inside the callback and observes its completion status |
| `--close-subscribed` | Keeps the event token subscribed and tests cleanup through frame pool closure |
| `--poll-only` | Allows polling for frames without event notifications as a comparison; passing this mode does not establish that event delivery works |

Without `--expect-shim`, the probe expects the native Windows 10 missing-interface error. Do not treat launching `dist/compat_probe.exe` without arguments beside the proxy as a default self-test. Use the standard commands above, or let `validate.ps1` prepare a baseline directory.

Exit codes are `0` for success, `1` for validation failure, and `2` for unknown or incomplete command-line arguments.

### Key output

- `proxy_loaded=1 iat_hooks=1`: The probe loaded the proxy and installed the import hook.
- `SetIsBorderRequired_hr=0x00000000`: The property call succeeded; the native baseline expects `0x80004002`.
- `frame_events` and `callback_thread`: The event count and the thread executing the callback.
- `expected_color_pixels`: The number of captured pixels matching the test blue. The check fails if this is less than one quarter of the image area.
- `software_bitmap=status_in_callback_1`: Conversion was already Completed inside the callback; `0` means Started.
- `live_handlers=0` and `dispatch_errors=0`: Cleanup found no live wrappers or recorded dispatch errors.

The probe limits how long it waits for frames, observes conversion status, and checks reference release. These limits cannot forcibly interrupt arbitrary driver or COM calls. If an underlying native call blocks, ending the test process may still be necessary.

## Validation through the official helper

After installing the DLL and restarting the corresponding helper:

1. Open `dist/capture_test_window.exe`.
2. Enumerate and select that window through Computer Use.
3. Request a screenshot and check the window title and blue test content.
4. Request more screenshots and resize the window to confirm that the returned image is current.
5. Separately check accessibility reads without screenshots, then close the test window.

The probe does not reproduce every official call path, so its result does not replace this step. Record the helper and DLL SHA-256 hashes, whether each operation succeeded, and its duration to compare builds.

## Using trace logs

Build a diagnostic version, then install it:

```powershell
.\build.ps1 -Trace
```

Use the same installation procedure as for a regular build: exit the old helper, uninstall the previous DLL, install the new DLL, and restart.

Look for `codex-capture-compat-<PID>.log` in the helper's temporary directory. The call sequence helps distinguish these cases:

| Observation | What it establishes |
| --- | --- |
| Expected logs are absent | A regular build may be loaded, hooks may not have been reached, or the log path may be unwritable; absence alone does not prove that the DLL was not loaded |
| Successful `StartCapture.leave` | The capture startup call returned success |
| `FrameArrived.add.deferred` | The subscription uses an asynchronous wrapper |
| `FrameArrived.add.native_fallback` | Wrapper creation failed and native registration was used; the HRESULT helps identify why |
| `Invoke.enter` followed by `TryGetNextFrame.frame` | The original callback started and obtained a valid frame |
| `Invoke.enter` without `Invoke.leave` for an extended period | Execution is blocked inside the original callback; the event did reach the callback |
| `Invoke.leave` returns an error | The original callback failed; inspect the HRESULT and dispatch statistics |

Logging overhead and additional calls can affect scheduling. Record regular and diagnostic builds separately when comparing performance; a single diagnostic run's timing is not a performance guarantee.

## Common issues

| Symptom | Checks and actions |
| --- | --- |
| `vswhere.exe` or MSVC is missing | Install or repair Visual Studio Installer and the C++ x64/x86 tools component |
| `IGraphicsCaptureSession3` or WinRT headers are missing | Check that the Windows SDK and C++/WinRT development headers are complete |
| A target EXE is in use during a build | Close this project's probe or test window, then rebuild |
| Installation refuses an existing `version.dll` | Determine its origin; uninstall this project's old version with the script before upgrading, and preserve unknown DLLs |
| The installation record is missing or the hash differs | Check for replaced files or an incorrect path; retain the files for inspection |
| The border interface is still reported missing after installation | Check the actual helper directory, whether the helper was restarted, and whether its loading behavior is still compatible |
| The probe fails while creating the capture target | Check for a normal interactive desktop; isolation, a locked screen, or a non-capturable target can affect this step |
| Screenshots still time out | Use logs to distinguish startup failure, absent events, failed frame retrieval, and blocking inside the callback |
| The probe passes but official calls fail | Verify the loaded DLL against the build hash, then inspect image conversion and window state in the official path |
| Uninstall reports that the file is in use | Exit helpers using the DLL in that directory, then retry; unrelated processes do not need to be terminated |
