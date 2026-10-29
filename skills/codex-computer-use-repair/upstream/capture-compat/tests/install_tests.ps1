$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $project ('validation\install-fixture-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$helper = Join-Path $fixture 'codex-computer-use.exe'
$dll = Join-Path $fixture 'version.dll'
$record = Join-Path $fixture 'codex-capture-compat.install.json'
$installer = Join-Path $project 'install.ps1'
Set-Content -LiteralPath $helper -Value 'Non-executable install test fixture.'
& $installer -HelperPath $helper -WhatIf
if ((Test-Path -LiteralPath $dll) -or (Test-Path -LiteralPath $record)) { throw 'WhatIf wrote files.' }
& $installer -HelperPath $helper
if (-not (Test-Path -LiteralPath $record)) { throw 'Install record missing.' }
if ((Get-FileHash -LiteralPath $dll).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'dist\version.dll')).Hash) {
    throw 'Copied DLL differs.'
}
& $installer -HelperPath $helper
& $installer -HelperPath $helper -Action Uninstall
if ((Test-Path -LiteralPath $dll) -or (Test-Path -LiteralPath $record)) { throw 'Uninstall left owned files.' }
Set-Content -LiteralPath $dll -Value 'Foreign DLL fixture - must not be overwritten or removed.'
$foreignHash = (Get-FileHash -LiteralPath $dll).Hash
$rejected = $false
try { & $installer -HelperPath $helper } catch { $rejected = $true }
if (-not $rejected -or (Get-FileHash -LiteralPath $dll).Hash -ne $foreignHash) { throw 'Foreign DLL overwrite guard failed.' }
$rejected = $false
try { & $installer -HelperPath $helper -Action Uninstall } catch { $rejected = $true }
if (-not $rejected -or (Get-FileHash -LiteralPath $dll).Hash -ne $foreignHash) { throw 'Unowned DLL removal guard failed.' }
'PASS: WhatIf, copy hash, idempotency, uninstall, foreign-DLL preservation.'
