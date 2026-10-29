[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][string]$HelperPath,
    [ValidateSet('Install','Uninstall')][string]$Action = 'Install'
)
$ErrorActionPreference = 'Stop'
$helper = (Resolve-Path -LiteralPath $HelperPath).Path
if ([IO.Path]::GetFileName($helper) -ine 'codex-computer-use.exe') {
    throw 'HelperPath must point to codex-computer-use.exe, not Codex.exe or a system DLL.'
}
$targetDir = [IO.Path]::GetDirectoryName($helper)
$target = Join-Path $targetDir 'version.dll'
$record = Join-Path $targetDir 'codex-capture-compat.install.json'
$source = Join-Path $PSScriptRoot 'dist\version.dll'

if ($Action -eq 'Install') {
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $target) {
        $currentHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
        if ($currentHash -eq $sourceHash -and (Test-Path -LiteralPath $record)) {
            Write-Output 'This build is already installed.'
            return
        }
        throw "An existing version.dll is present at $target. It will not be overwritten."
    }
    if (Test-Path -LiteralPath $record) { throw 'A previous install record exists; inspect it before installing.' }
    if ($PSCmdlet.ShouldProcess($target, 'Install local Win10 capture compatibility DLL')) {
        Copy-Item -LiteralPath $source -Destination $target
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sourceHash) {
            throw 'Installed DLL hash does not match the built file.'
        }
        [pscustomobject]@{
            helperPath=$helper
            helperSha256=(Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash
            dllSha256=$sourceHash
            installedAt=(Get-Date).ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath $record -Encoding utf8
        Write-Output "Installed: $target"
        Write-Output 'Restart the Computer Use helper / Codex to load the DLL.'
    }
} else {
    if (-not (Test-Path -LiteralPath $record)) { throw 'Install record missing; refusing to remove an unowned DLL.' }
    $installed = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    if (-not [string]::Equals($installed.helperPath, $helper, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Install record belongs to another helper path.'
    }
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $installed.dllSha256) {
            throw 'version.dll changed since installation; refusing to remove it.'
        }
    }
    if ($PSCmdlet.ShouldProcess($target, 'Uninstall the recorded local compatibility DLL')) {
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
        Remove-Item -LiteralPath $record
        Write-Output 'Removed this compatibility DLL. Restart the helper / Codex.'
    }
}
