# Regenerate the collection using native Amiga matches in isolated FS-UAE jobs.
# Each job prepares/saves the position, then closes; load the finished collection
# in the normal game to play. Existing games and player saves stay untouched.
param(
    [string]$Kickstart = $env:SWOS_KICKSTART,
    [string]$FsUae = $env:SWOS_FSUAE
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
foreach ($index in 1..6) {
    & "$PSScriptRoot/challenge_gen_amiga.ps1" `
        -Spec "$repo/docs/challenge_specs/collection/amiga_0$index.json" `
        -Output "$repo/bin/data/CCA0$index.DAT" `
        -Kickstart $Kickstart -FsUae $FsUae -Force
}
Write-Output 'Amiga collection ready: bin/data/CCA01.DAT through CCA06.DAT.'
