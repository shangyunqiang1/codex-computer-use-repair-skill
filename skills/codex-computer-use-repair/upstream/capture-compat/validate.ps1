[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$dist = Join-Path $PSScriptRoot 'dist'
$root = Join-Path $PSScriptRoot 'validation'
$baseline = Join-Path $root 'baseline'
$unrelated = Join-Path $root 'unrelated'
New-Item -ItemType Directory -Path $root,$baseline,$unrelated -Force | Out-Null
if (Test-Path -LiteralPath (Join-Path $baseline 'version.dll')) {
    throw 'Baseline directory contains a version.dll. Choose a clean validation directory.'
}
Copy-Item -LiteralPath (Join-Path $dist 'compat_probe.exe') -Destination $baseline -Force
Copy-Item -LiteralPath (Join-Path $dist 'compat_probe.exe') -Destination (Join-Path $unrelated 'unrelated_probe.exe') -Force
Copy-Item -LiteralPath (Join-Path $dist 'version.dll') -Destination $unrelated -Force
$results = [System.Collections.Generic.List[object]]::new()
function Run-Probe([string]$Name, [string]$Executable, [string[]]$Arguments) {
    $output = @(& $Executable @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $code = $LASTEXITCODE
    $results.Add([pscustomobject]@{name=$Name;exitCode=$code;output=$output})
    $output | Write-Output
    if ($code -ne 0) { throw "$Name failed (exit $code). Run in the normal interactive Windows desktop session." }
}
try {
    Run-Probe 'native Win10 baseline' (Join-Path $baseline 'compat_probe.exe') @()
    Run-Probe 'unrelated executable is not hooked' (Join-Path $unrelated 'unrelated_probe.exe') @()
    Run-Probe 'proxy and real WGC event capture' (Join-Path $dist 'compat_probe.exe') @('--expect-shim','--capture','--expect-deferred','--output',(Join-Path $root 'capture.bmp'))
    Run-Probe 'MTA without message pump' (Join-Path $dist 'compat_probe.exe') @('--expect-shim','--capture','--expect-deferred','--no-pump')
    Run-Probe 'STA without message pump' (Join-Path $dist 'compat_probe.exe') @('--expect-shim','--capture','--expect-deferred','--sta','--no-pump')
    Run-Probe 'late subscription after drain' (Join-Path $dist 'compat_probe.exe') @('--expect-shim','--capture','--expect-deferred','--late-subscribe')
    Run-Probe 'SoftwareBitmap completion inside callback' (Join-Path $dist 'compat_probe.exe') @('--expect-shim','--capture','--expect-deferred','--software-bitmap')
    Run-Probe 'close pool with active subscription' (Join-Path $dist 'compat_probe.exe') @('--expect-shim','--capture','--expect-deferred','--software-bitmap','--close-subscribed')
} finally {
    $osInfo = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    [pscustomobject]@{
        timestamp=(Get-Date).ToString('o')
        os=[pscustomobject]@{product=$osInfo.ProductName;displayVersion=$osInfo.DisplayVersion;build=$osInfo.CurrentBuild;ubr=$osInfo.UBR}
        dllSha256=(Get-FileHash -LiteralPath (Join-Path $dist 'version.dll') -Algorithm SHA256).Hash
        results=$results
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'report.json') -Encoding utf8
}
