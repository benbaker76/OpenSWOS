# Regenerate the six SDL collection assets with an isolated Windows save/config
# folder. The engine prepares and saves each position, then exits. Existing
# games and player saves are untouched. Keep the specs alongside these assets.
param([string]$Assets = "$env:USERPROFILE/Documents/SWOS/assets")
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$job = Join-Path $repo ('build/collection-gen/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $job -Force | Out-Null
$previousProfile = $env:USERPROFILE
$previousAssets = $env:SWOS_ASSETS
$previousPath = $env:PATH
try {
    $env:USERPROFILE = $job
    $env:SWOS_ASSETS = $Assets
    $env:PATH = 'C:\cygwin64\bin;' + $env:PATH
    Push-Location (Join-Path $repo 'bin')
    try {
        foreach ($index in 1..6) {
            $spec = Join-Path $repo "docs/challenge_specs/collection/sdl_0$index.json"
            & ./SWOS.exe "--challenge-gen=$spec"
            if ($LASTEXITCODE -ne 0) { throw "Challenge $index generation failed ($LASTEXITCODE)" }
            $file = Join-Path $job "Documents/SWOS/save/CCS0$index.CHL"
            if (!(Test-Path -LiteralPath $file)) { throw "Missing generated position: $file" }
            Copy-Item -LiteralPath $file -Destination "data/CCS0$index.DAT"
        }
    } finally { Pop-Location }
    Write-Output "Collection generated. Diagnostics and original CHL files: $job"
} finally {
    $env:USERPROFILE = $previousProfile
    $env:SWOS_ASSETS = $previousAssets
    $env:PATH = $previousPath
}
