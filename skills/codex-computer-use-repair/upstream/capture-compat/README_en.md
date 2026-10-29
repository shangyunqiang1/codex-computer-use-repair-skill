# Codex Capture Compat · Build and usage

[简体中文](README.md) | [English](README_en.md)

This directory contains the source, build scripts, and test tools for the Windows 10 Computer Use screenshot compatibility layer. See the [project README](../README_en.md) for an overview and the [implementation guide](docs/implementation_en.md) for internal details.

Run all commands below from this directory. Helper paths in examples are placeholders.

## Build requirements

| Dependency | Requirement |
| --- | --- |
| Windows | An x64 build environment; the full live regression suite targets Windows 10 |
| PowerShell | Must be able to run the project scripts and load the Visual Studio DevShell module |
| Visual Studio or Build Tools | MSVC x64/x86 C++ tools with C++17 support |
| MASM | Provides `ml64.exe` for the x64 export forwarding stubs |
| Windows SDK | Includes C++/WinRT headers, `IGraphicsCaptureSession3`, and Direct3D 11 development libraries |

The build script uses the Visual Studio Installer's `vswhere.exe` to find the latest installation containing `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`, then enters an x64 host / x64 target development environment. You generally do not need to configure environment variables in a Developer Command Prompt first.

The project compiles and links directly with MSVC. It does not depend on CMake, Cargo, or npm.

## Building

### Regular build

```powershell
.\build.ps1
```

The script compiles the DLL, tests, and probes, creates the `dist/` outputs, then runs `core_tests.exe` and `dispatch_tests.exe`. On success, it prints the test results and the DLL's SHA-256 hash. Compilation, linking, or test failures raise an error.

The main options are C++17, `/O2`, static CRT linking with `/MT`, and `/guard:cf`. Linking also enables `/dynamicbase` and `/nxcompat`.

### Diagnostic build

```powershell
.\build.ps1 -Trace
```

`-Trace` defines `CAPTURE_COMPAT_TRACE`, adding diagnostic hooks and text logging along the call path. It still uses optimized compilation and overwrites the same `build/` and `dist/` outputs; it does not create a separate Debug output directory.

Regular builds do not write Trace files. After switching back to a regular build, follow the upgrade procedure and restart the helper to update the DLL in use.

### Build outputs

| Path | Description |
| --- | --- |
| `dist/version.dll` | Proxy DLL for deployment |
| `dist/compat_probe.exe` | Standalone WGC probe |
| `dist/capture_test_window.exe` | Simple blue window for validation through official Computer Use |
| `build/core_tests.exe` | COM border compatibility tests |
| `build/dispatch_tests.exe` | Callback dispatch tests |
| `build/*.obj`, `build/proxy.lib`, etc. | Intermediate compilation and linking files |

Close running probes and test windows before rebuilding to avoid errors when replacing their executable files. Building does not automatically install the DLL.

## Testing

```powershell
# Installation logic tests using temporary files under validation/
.\tests\install_tests.ps1

# Real WGC capture tests; require a normal interactive desktop
.\validate.ps1
```

Live validation displays the probe's own window and creates `validation/report.json` and `validation/capture.bmp`. The baseline test explicitly expects Windows 10 to lack the border interface, so it is not a universal pass criterion across Windows versions.

See the [validation and troubleshooting guide](docs/validation_en.md) for probe parameters, output interpretation, and test coverage.

## Installation and usage

For a prebuilt release, preserve the ZIP's complete directory structure and run the installation commands from the extracted `capture-compat` directory. The package already contains all three binaries in `dist/`, so there is no need to build first. See the [CI and release guide](docs/ci_en.md) for downloads and checksum verification.

### Locate the target

Install beside the `codex-computer-use.exe` that is actually running. You can inspect its path while it is active:

```powershell
Get-Process -Name codex-computer-use -ErrorAction SilentlyContinue |
    Select-Object Id, Path
```

If multiple processes appear, identify the runtime used by the current Computer Use instance. Do not deploy to every old installation directory. If the process is stopped or access is insufficient, the list may be empty or omit the path.

### First installation

```powershell
$helperPath = 'C:\path\to\codex-computer-use.exe'

.\install.ps1 -HelperPath $helperPath -WhatIf

# Exit the running helper before proceeding
.\install.ps1 -HelperPath $helperPath -Action Install
```

`Install` is the default action, so `-Action Install` may be omitted. Restart Computer Use after installation and continue using its existing interfaces. The DLL does not run as a separate application.

The installer adds the proxy and installation record beside the existing helper:

```text
codex-computer-use.exe            Existing target helper
version.dll                      This project's proxy
codex-capture-compat.install.json Installation record
```

The proxy must be in the helper's directory. The script does not search all runtimes, change system directories or registry entries, or stop or restart processes.

### Installer parameters

| Parameter | Meaning |
| --- | --- |
| `-HelperPath <path>` | Required; must point to an existing file named `codex-computer-use.exe` |
| `-Action Install` | Installs `dist/version.dll` |
| `-Action Uninstall` | Removes this project's proxy using its installation record |
| `-WhatIf` | Previews operations without copying or deleting files; path and existing-file checks still run |

The installation record contains the helper path, helper hash, DLL hash, and installation time. The helper hash records the target build; uninstall checks the target path and DLL hash.

If the existing DLL differs from the file to install, the script refuses to overwrite it. If the hashes match and an installation record exists, it reports that the build is already installed. Do not resolve a conflict by directly overwriting a DLL of unknown origin.

### Upgrade

Build the new version, then exit the helper using the installed DLL:

```powershell
.\install.ps1 -HelperPath $helperPath -Action Uninstall
.\install.ps1 -HelperPath $helperPath -Action Install
```

Restart Computer Use afterward. Uninstall verifies the installed DLL against the previous installation record; it does not require that DLL to match the newly built file.

### Uninstall

```powershell
# Exit the helper using this DLL first
.\install.ps1 -HelperPath $helperPath -Action Uninstall
```

The script requires an installation record with a matching path and checks that the DLL's hash has not changed. It removes only the proxy and record, preserving the helper. Restart Computer Use afterward to restore the original loading behavior.

If the record is missing or the hash differs, determine the file's origin first. The script does not guess which DLL is safe to delete.

## Diagnostic logs

After installing a `-Trace` build, newly started target processes can write to:

```text
%TEMP%\codex-capture-compat-<PID>.log
```

`%TEMP%` refers to the helper process's own temporary directory. Logs contain milliseconds since system startup, thread IDs, operations, object addresses, and HRESULT values. They do not contain screenshots or window text. Failure to write a log does not interrupt the original call.

The read-only `CodexCaptureCompatGetStatus` export also exposes statistics for the calling process. See the [implementation guide](docs/implementation_en.md) for the interface and fields.

## Deployment constraints

- Natively supported border interfaces remain under system control. The fallback for a missing interface preserves the default system capture border.
- After updating the host application, recheck the helper path, imports, and DLL loading behavior.
- Replacing a file on disk does not update an already loaded module. Restart the corresponding helper.
- For uninstall failures, first check file usage and the installation record. See the [validation and troubleshooting guide](docs/validation_en.md) for specific errors.
