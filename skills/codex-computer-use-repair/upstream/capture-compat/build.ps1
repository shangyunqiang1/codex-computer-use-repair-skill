[CmdletBinding()]
param([switch]$Trace)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe not found.' }
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC x64 tools not found.' }
Import-Module (Join-Path $installation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$build = Join-Path $PSScriptRoot 'build'
$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Path $build,$dist -Force | Out-Null
Push-Location $build
try {
    $flags = @('/nologo', '/std:c++17', '/EHsc', '/W4', '/O2', '/MT', '/utf-8', '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/guard:cf')
    if ($Trace) { $flags += '/DCAPTURE_COMPAT_TRACE' }
    & cl.exe @flags /c (Join-Path $PSScriptRoot 'src\compat.cpp') (Join-Path $PSScriptRoot 'src\proxy.cpp') (Join-Path $PSScriptRoot 'src\frame_dispatch.cpp')
    if ($LASTEXITCODE) { throw 'DLL C++ compilation failed.' }
    & ml64.exe /nologo /c (Join-Path $PSScriptRoot 'src\version.asm')
    if ($LASTEXITCODE) { throw 'MASM assembly failed.' }
    & link.exe /nologo /dll /machine:x64 /dynamicbase /nxcompat /guard:cf '/out:version.dll' '/implib:proxy.lib' "/def:$(Join-Path $PSScriptRoot 'src\version.def')" compat.obj proxy.obj frame_dispatch.obj version.obj windowsapp.lib
    if ($LASTEXITCODE) { throw 'DLL linking failed.' }
    & cl.exe @flags (Join-Path $PSScriptRoot 'tests\core_tests.cpp') compat.obj frame_dispatch.obj /Fe:core_tests.exe /link windowsapp.lib /dynamicbase /nxcompat
    if ($LASTEXITCODE) { throw 'Core test compilation failed.' }
    & cl.exe @flags (Join-Path $PSScriptRoot 'tests\dispatch_tests.cpp') frame_dispatch.obj /Fe:dispatch_tests.exe /link windowsapp.lib /dynamicbase /nxcompat
    if ($LASTEXITCODE) { throw 'Dispatch test compilation failed.' }
    & cl.exe @flags (Join-Path $PSScriptRoot 'tests\probe.cpp') /Fe:compat_probe.exe /link windowsapp.lib d3d11.lib version.lib user32.lib gdi32.lib /dynamicbase /nxcompat
    if ($LASTEXITCODE) { throw 'Probe compilation failed.' }
    & cl.exe @flags (Join-Path $PSScriptRoot 'tests\test_window.cpp') /Fe:capture_test_window.exe /link user32.lib gdi32.lib /subsystem:windows /dynamicbase /nxcompat
    if ($LASTEXITCODE) { throw 'Test window compilation failed.' }
    Copy-Item -LiteralPath (Join-Path $build 'capture_test_window.exe') -Destination $dist -Force
    Copy-Item -LiteralPath (Join-Path $build 'version.dll'),(Join-Path $build 'compat_probe.exe') -Destination $dist -Force
    & (Join-Path $build 'core_tests.exe')
    if ($LASTEXITCODE) { throw 'Core compatibility tests failed.' }
    & (Join-Path $build 'dispatch_tests.exe')
    if ($LASTEXITCODE) { throw 'Frame dispatch tests failed.' }
    Get-FileHash -LiteralPath (Join-Path $dist 'version.dll') -Algorithm SHA256 | Format-List
} finally { Pop-Location }
