# Generate a native Amiga .CHL under FS-UAE, then copy it to the normal save folder.
# The emulator has its own mounted drawer; existing games and saves are untouched.
param(
    [Parameter(Mandatory=$true)][string]$Spec,
    [string]$Output,
    [string]$Kickstart = $env:SWOS_KICKSTART,
    [string]$FsUae = $env:SWOS_FSUAE,
    [int]$TimeoutSeconds = 300,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $repo 'bin'
$job = Get-Content -LiteralPath $Spec -Raw | ConvertFrom-Json
if (-not $Output) {
    $name = if ($job.out) { [string]$job.out } else { 'AMIGAGEN.CHL' }
    if ($name -notmatch '^[A-Za-z0-9_-]{1,30}\.CHL$') {
        throw 'out must be a simple .CHL filename, or supply -Output.'
    }
    $Output = Join-Path (Join-Path $bin 'save') $name
}
$Output = [IO.Path]::GetFullPath($Output)
if ((Test-Path -LiteralPath $Output) -and -not $Force) {
    throw "Output already exists: $Output. Choose another filename or use -Force."
}
if (-not $FsUae) {
    $FsUae = "$env:USERPROFILE\tools\vscode-amiga-debug\bin\win32\fs-uae\fs-uae.exe"
}
if (-not $Kickstart) {
    $rom = Get-ChildItem "$env:USERPROFILE\Downloads\kickstarts" -Filter '*A1200*.rom' |
        Sort-Object Name | Select-Object -Last 1
    if ($rom) { $Kickstart = $rom.FullName }
}
if (-not $Kickstart -or -not (Test-Path -LiteralPath $Kickstart)) { throw 'Supply -Kickstart <A1200 ROM>.' }
if (-not (Test-Path -LiteralPath $FsUae)) { throw 'Supply -FsUae <fs-uae.exe>.' }
if (-not (Test-Path -LiteralPath (Join-Path $bin 'SWOS2'))) { throw 'Build bin/SWOS2 first (docs/LOCAL_WORKFLOW.md).' }

$drawer = Join-Path $repo ('build\challenge-amiga\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $drawer | Out-Null
foreach ($dir in @('c', 's', 'data', 'grafs', 'grafs_aga', 'sound')) {
    Copy-Item -LiteralPath (Join-Path $bin $dir) -Destination $drawer -Recurse
}
Copy-Item -LiteralPath (Join-Path $bin 'SWOS2') -Destination $drawer
New-Item -ItemType Directory -Path (Join-Path $drawer 'save') | Out-Null
$job | Add-Member -NotePropertyName out -NotePropertyValue 'RESULT.CHL' -Force
$specPath = Join-Path $drawer 'data\CHALGEN.DAT'
[IO.File]::WriteAllText($specPath, ($job | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
$statusPath = Join-Path $drawer 'data\CHALGEN.LOG'
if (Test-Path -LiteralPath $statusPath) { Remove-Item -LiteralPath $statusPath }
$cfg = Join-Path $drawer 'generate.fs-uae'
@"
amiga_model = A1200
kickstart_file = $Kickstart
hard_drive_0 = $drawer
chip_memory = 2048
fast_memory = 8192
fullscreen = 0
window_width = 960
window_height = 600
"@ | Set-Content -LiteralPath $cfg -Encoding ASCII

Write-Host "Generating Amiga challenge in $drawer"
Write-Host 'This is the generation run: its emulator closes after saving. Load the resulting .CHL to play.'
$emu = Start-Process -FilePath $FsUae -ArgumentList ('"' + $cfg + '"') -WorkingDirectory (Split-Path $FsUae) -WindowStyle Hidden -PassThru
$timer = [Diagnostics.Stopwatch]::StartNew()
$previous = ''
try {
    while ($true) {
        $status = if (Test-Path -LiteralPath $statusPath) { Get-Content -LiteralPath $statusPath -Raw } else { '' }
        if ($status -and $status -ne $previous) {
            Write-Host $status.Trim()
            $previous = $status
        }
        if ($status.StartsWith('FAILED:')) { throw $status.Trim() }
        if ($status.StartsWith('DONE:')) { break }
        if ($emu.HasExited) { throw "Emulator exited before generation completed. Diagnostics: $drawer" }
        if ($timer.Elapsed.TotalSeconds -gt $TimeoutSeconds) { throw "Generation timed out. Last status: $status. Diagnostics: $drawer" }
        Start-Sleep -Milliseconds 500
    }
    $result = Join-Path $drawer 'save\RESULT.CHL'
    $bytes = [IO.File]::ReadAllBytes($result)
    if ($bytes.Length -lt 96 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'SWCH') {
        throw 'Generator did not produce a valid challenge header.'
    }
    $minute = 256 * [int]$bytes[42] + $bytes[43]
    $score1 = 256 * [int]$bytes[44] + $bytes[45]
    $score2 = 256 * [int]$bytes[46] + $bytes[47]
    $want1 = @($job.scorers | Where-Object { $_.side -eq 1 }).Count
    $want2 = @($job.scorers | Where-Object { $_.side -eq 2 }).Count
    if ($minute -ne $job.minute -or $score1 -ne $want1 -or $score2 -ne $want2) {
        throw "Generated header disagrees with spec: ${score1}:${score2} at $minute."
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
    # Recheck at copy time: another job could have produced the same name meanwhile.
    [IO.File]::Copy($result, $Output, [bool]$Force)
    Write-Host "Created $Output ($($bytes.Length) bytes): ${score1}:${score2} at minute $minute."
} finally {
    # This process belongs to this job. Give normal hardware cleanup a chance first.
    if (-not $emu.HasExited -and -not $emu.WaitForExit(5000)) {
        Write-Host 'Closing this generator job emulator.'
        Stop-Process -Id $emu.Id -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $specPath -ErrorAction SilentlyContinue
}
