# Bring the current ESP32-S3 4848S040 firmware into an isolated Git checkout.
# Does not modify this historical repository or flash any device.
[CmdletBinding()]
param([switch]$Build)

$ErrorActionPreference = 'Stop'
$SourceUrl = 'https://github.com/wpv10barza/erp-mantto-esp32.git'
$Target = Join-Path $HOME 'erp-mantto-esp32-deploy'
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git for Windows is required.'
}

if (-not (Test-Path -LiteralPath $Target)) {
    & git clone --branch main -- $SourceUrl $Target
    if ($LASTEXITCODE -ne 0) { throw 'Failed to clone canonical firmware.' }
} else {
    $origin = (& git -C $Target remote get-url origin).Trim()
    if ($LASTEXITCODE -ne 0 -or $origin -notmatch 'github\.com[/:]wpv10barza/erp-mantto-esp32(?:\.git)?$') {
        throw "Existing directory has wrong origin: $Target"
    }
    $dirty = @(& git -C $Target status --porcelain --untracked-files=no | Where-Object { $_.Trim() })
    if ($LASTEXITCODE -ne 0 -or $dirty.Count -gt 0) {
        throw 'Canonical checkout contains local tracked edits: resolve without deleting data.'
    }
    $branch = (& git -C $Target branch --show-current).Trim()
    if ($branch -ne 'main') {
        throw 'Canonical checkout is not on main; do not switch over local history automatically.'
    }
    & git -C $Target pull --ff-only origin main
    if ($LASTEXITCODE -ne 0) { throw 'Non-fast-forward: preserve local commits; no reset or clean.' }
}

$sha = (& git -C $Target rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sha -notmatch '^[0-9a-f]{40}$') {
    throw 'Invalid Git commit SHA.'
}
Write-Host "[OK] Canonical ESP32-S3 4848S040 firmware main: $sha"
Write-Host "[PATH] $Target"
Write-Host '[SAFE] Historical firmware/backend source unchanged. No USB flash performed.'

if ($Build) {
    if (-not (Get-Command py -ErrorAction SilentlyContinue)) { throw 'Python launcher py not found.' }
    & py -m platformio run -d $Target -e panel_4848s040
    if ($LASTEXITCODE -ne 0) { throw 'Canonical firmware build failed. No flash.' }
    Write-Host '[OK] Canonical firmware compiled; USB flash is a separate physical operation.'
}
