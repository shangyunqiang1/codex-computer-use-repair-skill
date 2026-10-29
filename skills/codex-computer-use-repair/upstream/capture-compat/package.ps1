[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9.+-]{0,95}$')][string]$Version = 'dev',
    [switch]$Trace,
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$suffix = if ($Trace) { '-trace' } else { '' }
$packageName = "CodexCaptureCompat-$Version-windows-x64$suffix"

# Keep the source layout so installation, validation, and documentation links
# work directly from an extracted archive. Never copy build or validation data.
$files = [System.Collections.Generic.List[string]]::new()
foreach ($relative in @(
    'LICENSE', 'README.md', 'README_en.md', '.gitignore',
    'capture-compat/.gitignore', 'capture-compat/README.md', 'capture-compat/README_en.md',
    'capture-compat/build.ps1',
    'capture-compat/install.ps1', 'capture-compat/validate.ps1',
    'capture-compat/package.ps1', 'capture-compat/dist/version.dll',
    'capture-compat/dist/compat_probe.exe', 'capture-compat/dist/capture_test_window.exe'
)) { $files.Add($relative) }

foreach ($group in @(
    @{directory='capture-compat/src'; extensions=@('.cpp', '.h', '.asm', '.def')},
    @{directory='capture-compat/tests'; extensions=@('.cpp', '.ps1')},
    @{directory='capture-compat/docs'; extensions=@('.md')},
    @{directory='.github/workflows'; extensions=@('.yml', '.yaml')}
)) {
    Get-ChildItem -LiteralPath (Join-Path $root $group.directory) -File -Recurse |
        Where-Object { $_.Extension -in $group.extensions } |
        ForEach-Object { $files.Add($_.FullName.Substring($root.Length + 1).Replace('\', '/')) }
}
foreach ($relative in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        throw "Package input missing: $relative. Build the project before packaging."
    }
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$archivePath = Join-Path $outputRoot "$packageName.zip"
$checksumPath = "$archivePath.sha256"
if ((Test-Path -LiteralPath $archivePath) -or (Test-Path -LiteralPath $checksumPath)) {
    throw "Package already exists: $packageName. Use a new version or output directory."
}
$stagePath = Join-Path $outputRoot ('.stage-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stagePath | Out-Null
try {
    foreach ($relative in $files) {
        $destination = Join-Path $stagePath $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $destination
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stagePath, $archivePath)
    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    # Use LF on Windows too, for sha256sum in the Linux release job.
    [IO.File]::WriteAllText($checksumPath, "$hash  $packageName.zip" + [char]10, [Text.Encoding]::ASCII)
    [pscustomobject]@{archive=$archivePath; checksum=$checksumPath; sha256=$hash}
} finally {
    # Resolve and check the exact generated staging directory before removal.
    $resolvedStage = (Resolve-Path -LiteralPath $stagePath).Path
    $outputPrefix = $outputRoot.TrimEnd([char[]]@('\', '/')) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedStage.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a staging directory outside the package output directory.'
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
