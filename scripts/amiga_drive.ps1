# amiga_drive.ps1 -- drive the Amiga (m68k) build under FS-UAE from a script.
#
# The SDL counterpart is scripts/swos_drive.ps1 (docs/UI_DRIVER.md); this is the same
# idea for the emulated target: inject real OS key events into the FS-UAE window, grab
# the window to PNG, and collect the diagnostic files the game writes into bin/. The
# Amiga build writes NO log, so screenshots are the only ground truth.
# See docs/AMIGA_DRIVER.md.
#
#   scripts\amiga_drive.ps1 -Route friendly        # cold boot -> live match -> back to menu
#   scripts\amiga_drive.ps1 -Route challenge       # cold boot -> CHAL01.CHL loaded -> back
#   scripts\amiga_drive.ps1 -Launch -Shot -Tag boot
#   scripts\amiga_drive.ps1 -Keys "DOWN,DOWN,FIRE,WAIT800,SHOT:mymenu"
#   scripts\amiga_drive.ps1 -WaitMenu -Shot -Tag menu
#   scripts\amiga_drive.ps1 -CollectDiags -Tag after
#   scripts\amiga_drive.ps1 -Shutdown
#
# Keys: UP DOWN LEFT RIGHT | FIRE (joystick port 1 fire -- Right Ctrl by default)
#       ENTER (the Amiga keyboard's own menu fire) | ESC SPACE | SHOT[:label] | WAIT<ms>
#       plus raw names for probing: RCTRL LCTRL RALT LALT NUM0 NUM5 NUMENTER RSHIFT...
param(
    [int]$EmulatorId = 0,       # target one existing session (e.g. while generators run)
    [string]$Keys = "",
    [ValidateSet("", "friendly", "challenge")]
    [string]$Route = "",
    [switch]$Launch,
    [switch]$Shutdown,
    [switch]$Shot,
    [switch]$WaitMenu,
    [switch]$CollectDiags,
    [switch]$NoLaunch,            # -Route: drive the instance already running
    [switch]$Stay,                # -Route: stay in the match, don't ESC back to the menu
    [switch]$KeepShots,           # -Route: keep the shots already in the shot dir
    [int]$KeyDelayMs = 250,
    [int]$HoldMs = 120,
    [int]$BootWaitMs = 13000,     # launch -> first FIRE; must land INSIDE the loader intro
    [string]$ShotDir = "",
    [string]$Tag = "",
    [string]$Compare = "",        # compare the shots just taken against this baseline dir/file
    [string]$SaveBaseline = "",   # copy the shots just taken into this dir, as the baseline
    [int]$DiffPct = 2,            # a shot differing by more than this % of pixels is a FAIL
    [switch]$Check,               # make a route's own verdict decide the exit code
    [string]$Classify = "",       # just classify the shots in this dir and print the verdict
    [string]$FireKey = "RCTRL",
    [string]$Kickstart = $env:SWOS_KICKSTART,
    [string]$FsUae = $env:SWOS_FSUAE
)

$ErrorActionPreference = "Stop"
if ($EmulatorId -and ($Launch -or ($Route -and -not $NoLaunch))) {
    throw '-EmulatorId targets an existing session; use -NoLaunch with routes and omit -Launch.'
}
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Bin      = Join-Path $RepoRoot "bin"
if (-not $ShotDir) { $ShotDir = Join-Path $RepoRoot "build\amiga_shots" }
# Small big-endian dumps the game writes next to itself; the Amiga build has no log,
# so these plus the screenshots are the evidence a run leaves behind.
$DiagFiles = @("CHALDBG.BIN", "CHALCAM.BIN", "PITCH.BIN", "RTRIP.BIN", "LAYOUT.BIN")

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class AmigaDrv {
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit, Size=40)]
    public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int x, y; }

    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    public delegate bool EnumProc(IntPtr h, IntPtr p);

    /* FS-UAE titles its window "FSEMU ..." (the fsemu frontend). Match on the pid AND
     * the title: the process owns other windows, and grabbing the wrong one is a black
     * PNG that looks like a hung emulator. */
    public static IntPtr FindEmuWindow(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint wpid; GetWindowThreadProcessId(h, out wpid);
            if (wpid != pid || !IsWindowVisible(h)) return true;
            StringBuilder sb = new StringBuilder(300);
            GetWindowText(h, sb, 300);
            string t = sb.ToString();
            if (t.StartsWith("FSEMU") || t.StartsWith("FS-UAE")) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    /* SetForegroundWindow from a background process is blocked by Windows; the
     * AttachThreadInput dance is what makes it work. Without focus the keys go
     * nowhere and the emulator merely LOOKS hung. */
    public static bool Focus(IntPtr hwnd) {
        ShowWindow(hwnd, 9);               // SW_RESTORE
        BringWindowToTop(hwnd);
        SetWindowPos(hwnd, new IntPtr(-1), 0,0,0,0, 0x0001|0x0002|0x0040);
        SetWindowPos(hwnd, new IntPtr(-2), 0,0,0,0, 0x0001|0x0002|0x0040);

        IntPtr fg = GetForegroundWindow();
        uint dummy;
        uint fgThread  = GetWindowThreadProcessId(fg, out dummy);
        uint myThread  = GetCurrentThreadId();
        uint tgtThread = GetWindowThreadProcessId(hwnd, out dummy);

        AttachThreadInput(myThread, fgThread, true);
        AttachThreadInput(myThread, tgtThread, true);
        bool ok = SetForegroundWindow(hwnd);
        AttachThreadInput(myThread, tgtThread, false);
        AttachThreadInput(myThread, fgThread, false);

        System.Threading.Thread.Sleep(300);
        return GetForegroundWindow() == hwnd;
    }

    public static bool IsFocused(IntPtr h) { return GetForegroundWindow() == h; }

    /* Cheap guard for the per-key / per-shot case: the full Focus() dance sleeps, so
     * only pay for it when something else has actually taken the foreground. */
    public static bool EnsureFocus(IntPtr h) {
        if (GetForegroundWindow() == h) return true;
        return Focus(h);
    }

    public static string FgTitle() {
        StringBuilder sb = new StringBuilder(300);
        GetWindowText(GetForegroundWindow(), sb, 300);
        return sb.ToString();
    }

    public static string TitleOf(IntPtr h) {
        StringBuilder sb = new StringBuilder(300);
        GetWindowText(h, sb, 300);
        return sb.ToString();
    }

    /* Client area in screen coordinates -- the GL surface alone, so a grab holds no
     * window border and no desktop. */
    public static int[] ClientBox(IntPtr h) {
        RECT c; GetClientRect(h, out c);
        POINT p; p.x = 0; p.y = 0; ClientToScreen(h, ref p);
        return new int[] { p.x, p.y, c.right - c.left, c.bottom - c.top };
    }

    /* SendInput with KEYEVENTF_SCANCODE: SDL (which FS-UAE uses) reads scancodes, and
     * PostMessage never reaches it. */
    public static void Tap(ushort vk, bool ext, int holdMs) {
        ushort sc = (ushort)MapVirtualKey(vk, 0);
        TapScan(sc, ext, holdMs);
    }

    /* Numpad Enter shares VK_RETURN's scancode (0x1C) and is the extended one, so a
     * few keys have to be named by scancode rather than by virtual key. */
    public static void TapScan(ushort sc, bool ext, int holdMs) {
        uint f = 0x0008u | (ext ? 0x0001u : 0u);   // SCANCODE | EXTENDEDKEY
        INPUT[] d = new INPUT[1]; d[0].type = 1; d[0].ki.wScan = sc; d[0].ki.dwFlags = f;
        SendInput(1, d, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(holdMs);
        INPUT[] u = new INPUT[1]; u[0].type = 1; u[0].ki.wScan = sc; u[0].ki.dwFlags = f | 0x0002u;
        SendInput(1, u, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@

# The emulator window is DPI-scaled (125% here): without this, GetClientRect and
# ClientToScreen hand back LOGICAL pixels while CopyFromScreen works in PHYSICAL
# ones, and the grab lands a couple of hundred pixels up and left of the window --
# half desktop, half game. Must run before anything reads a window rectangle.
[void][AmigaDrv]::SetProcessDPIAware()

# name -> virtual key, extended?
$KeyMap = @{
    "UP"       = @(0x26, $true)
    "DOWN"     = @(0x28, $true)
    "LEFT"     = @(0x25, $true)
    "RIGHT"    = @(0x27, $true)
    "ENTER"    = @(0x0D, $false)
    "ESC"      = @(0x1B, $false)
    "SPACE"    = @(0x20, $false)
    "RCTRL"    = @(0xA3, $true)
    # letters, for in-match keys like C (save a position)
    "A"        = @(0x41, $false)
    "B"        = @(0x42, $false)
    "C"        = @(0x43, $false)
    "D"        = @(0x44, $false)
    "E"        = @(0x45, $false)
    "F"        = @(0x46, $false)
    "G"        = @(0x47, $false)
    "H"        = @(0x48, $false)
    "I"        = @(0x49, $false)
    "J"        = @(0x4A, $false)
    "K"        = @(0x4B, $false)
    "L"        = @(0x4C, $false)
    "M"        = @(0x4D, $false)
    "N"        = @(0x4E, $false)
    "O"        = @(0x4F, $false)
    "Q"        = @(0x51, $false)
    "R"        = @(0x52, $false)
    "S"        = @(0x53, $false)
    "T"        = @(0x54, $false)
    "U"        = @(0x55, $false)
    "V"        = @(0x56, $false)
    "W"        = @(0x57, $false)
    "Y"        = @(0x59, $false)
    "LCTRL"    = @(0xA2, $false)
    "RALT"     = @(0xA5, $true)
    "LALT"     = @(0xA4, $false)
    "RSHIFT"   = @(0xA1, $false)
    "LSHIFT"   = @(0xA0, $false)
    "NUM0"     = @(0x60, $false)
    "NUM1"     = @(0x61, $false)
    "NUM2"     = @(0x62, $false)
    "NUM4"     = @(0x64, $false)
    "NUM5"     = @(0x65, $false)
    "NUM6"     = @(0x66, $false)
    "NUM8"     = @(0x68, $false)
    "NUMENTER" = @(0x0D, $true)
    "F11"      = @(0x7A, $false)
    "F12"      = @(0x7B, $false)
    "Z"        = @(0x5A, $false)
    "X"        = @(0x58, $false)
    "P"        = @(0x50, $false)
}

# The instance THIS run launched. Without it, a second emulator on the machine (someone
# else's, or one left over) makes the script drive one window and watch another -- which
# shows up as "fs-uae exited" in the middle of a route that is going fine.
$script:EmuPid = $EmulatorId
function Get-Emu {
    if ($script:EmuPid) {
        $p = Get-Process -Id $script:EmuPid -ErrorAction SilentlyContinue
        if ($p) { return $p }
        return $null
    }
    return (Get-Process fs-uae -ErrorAction SilentlyContinue | Select-Object -First 1)
}

function Stop-Emu {
    if ($EmulatorId) {
        Stop-Process -Id $EmulatorId -Force -ErrorAction SilentlyContinue
        return
    }
    Get-Process fs-uae -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 600
}

function Resolve-FsUae {
    if ($FsUae -and (Test-Path $FsUae)) { return $FsUae }
    $cands = @(
        "$env:USERPROFILE\tools\vscode-amiga-debug\bin\win32\fs-uae\fs-uae.exe"
        "$env:USERPROFILE\.vscode\extensions\bartmanabyss.amiga-debug-*\bin\win32\fs-uae\fs-uae.exe"
    )
    foreach ($c in $cands) {
        $hit = Get-Item $c -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    throw "fs-uae.exe not found. Pass -FsUae <path> or set `$env:SWOS_FSUAE."
}

function Resolve-Kickstart {
    if ($Kickstart -and (Test-Path $Kickstart)) { return $Kickstart }
    $guess = Get-ChildItem "$env:USERPROFILE\Downloads\kickstarts" -Filter "*A1200*.rom" -ErrorAction SilentlyContinue |
             Sort-Object Name | Select-Object -Last 1
    if ($guess) { return $guess.FullName }
    throw "No A1200 Kickstart ROM. Pass -Kickstart <file.rom> or set `$env:SWOS_KICKSTART."
}

function Start-Emu {
    $exe = Resolve-FsUae
    $rom = Resolve-Kickstart
    # FS-UAE does NOT notice a relink: always kill it before starting, or you are
    # testing the binary from last time.
    $existing = @(Get-Process fs-uae -ErrorAction SilentlyContinue)
    if ($existing.Count) {
        Write-Host "note     : killing $($existing.Count) fs-uae already running (pid $($existing.Id -join ', '))"
    }
    Stop-Emu
    New-Item -ItemType Directory -Force -Path (Join-Path $Bin "save") | Out-Null
    if (-not (Test-Path (Join-Path $Bin "SWOS2"))) {
        throw "bin/SWOS2 is not built -- see docs/LOCAL_WORKFLOW.md."
    }
    $cfg = Join-Path ([System.IO.Path]::GetTempPath()) "swos-drive-a1200.fs-uae"
    # window 1440x900: a 960x600 window dies in glViewport ("Not a valid drawable size").
    # joystick_port_1 = keyboard is what puts the Amiga's joystick port (the one SWOS
    # reads for player 1 -- JOY1DAT + CIAA_PRA bit 7) on the host keyboard. Without it
    # there is no fire button at all and the loader intro cannot be skipped.
    @"
# generated by scripts/amiga_drive.ps1 -- do not edit
amiga_model = A1200
kickstart_file = $rom
hard_drive_0 = $Bin
chip_memory = 2048
fast_memory = 8192
fullscreen = 0
window_width = 1440
window_height = 900
joystick_port_1 = keyboard
joystick_port_1_mode = joystick
joystick_port_0_mode = mouse
"@ | Set-Content -Path $cfg -Encoding ASCII
    Write-Host "fs-uae   : $exe"
    Write-Host "kickstart: $rom"
    Write-Host "DH0      : $Bin  (startup-sequence -> SYS:SWOS2)"
    $proc = Start-Process -FilePath $exe -ArgumentList $cfg -WorkingDirectory (Split-Path $exe) -PassThru
    $script:EmuPid = $proc.Id
    Write-Host "pid      : $($proc.Id)"
    Start-Sleep -Milliseconds 2500
}

function Get-Window {
    $proc = Get-Emu
    if (-not $proc) { throw "fs-uae is not running (use -Launch)." }
    $hwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 40; $i++) {
        $hwnd = [AmigaDrv]::FindEmuWindow([uint32]$proc.Id)
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw "no FSEMU window for pid $($proc.Id)." }
    return $hwnd
}

# "Did OUR emulator go away?" is worth answering precisely: the usual cause is a
# SECOND fs-uae being started while a route runs (the VSCode extension's run button
# does that, and it kills the one already there), and the symptom without this check
# is a grab of a 0x0 window or keys vanishing into a dead process.
function Assert-Emu([string]$what) {
    if (Get-Emu) { return }
    $others = @(Get-Process fs-uae -ErrorAction SilentlyContinue)
    $msg = "the emulator this run launched (pid $($script:EmuPid)) has exited, during: $what"
    if ($others.Count) {
        $msg += ". Another fs-uae IS running (pid $($others.Id -join ', ')) -- something else " +
                "started an emulator and killed ours. Close it and run the route again."
    }
    throw $msg
}

function Grab([IntPtr]$hwnd) {
    Assert-Emu "screenshot"
    $box = [AmigaDrv]::ClientBox($hwnd)
    $x = $box[0]; $y = $box[1]; $w = $box[2]; $h = $box[3]
    if ($w -le 0 -or $h -le 0) { throw "window client area is ${w}x${h}" }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    return $bmp
}

# FSEMU paints its own title bar and two LED panels around the emulated screen. The
# title bar is the only part with a fixed height, and the Amiga picture is a 4:3 box
# centred in what is left -- so find the title bar by colour and derive the rest.
function Get-EmuRect([System.Drawing.Bitmap]$bmp) {
    $top = $bmp.GetPixel(2, 2)
    $ty = 0
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        $p = $bmp.GetPixel([int]($bmp.Width / 2), $y)
        if ([math]::Abs($p.R - $top.R) -gt 12 -or [math]::Abs($p.G - $top.G) -gt 12 -or
            [math]::Abs($p.B - $top.B) -gt 12) { $ty = $y; break }
    }
    $eh = $bmp.Height - $ty
    $ew = [int]($eh * 4 / 3)
    if ($ew -gt $bmp.Width) { $ew = $bmp.Width }
    $ex = [int](($bmp.Width - $ew) / 2)
    return @($ex, $ty, $ew, $eh)
}

# "Are we on the main menu?" -- the only state check the harness needs, and the Amiga
# build gives us no log to ask. The row across LOAD OLD COMPETITION is strongly
# red-dominant there and on no other screen this harness visits (measured over every
# screenshot of a full friendly + challenge run: menu R=119 G=47 B=71; a live match
# R=127 G=153 B=0; team selection R=0 G=0 B=78).
function Test-MainMenu([System.Drawing.Bitmap]$bmp) {
    $r = Get-EmuRect $bmp
    $ex = $r[0]; $ty = $r[1]; $ew = $r[2]; $eh = $r[3]
    $y = $ty + [int](0.79 * $eh)
    $sr = 0; $sg = 0; $sb = 0; $n = 0
    for ($i = 0.20; $i -lt 0.80; $i += 0.01) {
        $c = $bmp.GetPixel($ex + [int]($i * $ew), $y)
        $sr += $c.R; $sg += $c.G; $sb += $c.B; $n++
    }
    $mr = $sr / $n; $mg = $sg / $n; $mb = $sb / $n
    return (($mr -gt 90) -and ($mr -gt 2.0 * $mg) -and ($mr -gt 1.3 * $mb))
}

$script:ShotIndex = -1
function Take-Shot([IntPtr]$hwnd, [string]$label) {
    # Re-focus before every grab. CopyFromScreen copies whatever is ON TOP of that
    # screen rectangle, so anything that steals the foreground mid-run (a terminal you
    # type in, a notification) is silently captured INSTEAD of the game -- a shot of
    # somebody's editor that looks like a frozen emulator.
    if (-not [AmigaDrv]::EnsureFocus($hwnd)) { Write-Host "!! lost focus to '$([AmigaDrv]::FgTitle())'" }
    New-Item -ItemType Directory -Force -Path $ShotDir | Out-Null
    if ($script:ShotIndex -lt 0) {
        $max = 0
        Get-ChildItem (Join-Path $ShotDir "*.png") -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_.BaseName -match "^(\d+)") { $n = [int]$Matches[1]; if ($n -gt $max) { $max = $n } }
        }
        $script:ShotIndex = $max
    }
    $script:ShotIndex++
    $name = "{0:d3}" -f $script:ShotIndex
    if ($label) { $name = "${name}_$label" }
    $path = Join-Path $ShotDir "$name.png"
    $bmp = Grab $hwnd
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "shot: $path"
    return $path
}

function Send-Key([string]$k, [IntPtr]$hwnd = 0) {
    if ($k -eq "FIRE") { $k = $FireKey.ToUpper() }
    if (-not (Get-Emu)) { Write-Host "!! fs-uae exited before key '$k'"; return }
    # Re-focus before EVERY key, not just once per run: a key goes to whatever window
    # is in front, so anything that steals the foreground mid-route (you typing in a
    # terminal, a notification) sends the rest of the route somewhere else -- and the
    # game just looks like it ignored the press.
    if ($hwnd -ne [IntPtr]::Zero -and -not [AmigaDrv]::EnsureFocus($hwnd)) {
        Write-Host "!! key '$k' may be lost -- foreground is '$([AmigaDrv]::FgTitle())'"
    }
    if (-not $KeyMap.ContainsKey($k)) { Write-Host "unknown key '$k'"; return }
    $vk, $ext = $KeyMap[$k]
    if ($k -eq "NUMENTER") { [AmigaDrv]::TapScan([uint16]0x1C, $true, $HoldMs) }
    else { [AmigaDrv]::Tap([uint16]$vk, [bool]$ext, $HoldMs) }
    Start-Sleep -Milliseconds $KeyDelayMs
}

function Send-Keys([IntPtr]$hwnd, [string]$spec) {
    foreach ($raw in ($spec -split ",")) {
        $raw = $raw.Trim()
        if ($raw -eq "") { continue }
        if ($raw -match "^(?i)SHOT(?::(.+))?$") { Take-Shot $hwnd $Matches[1] | Out-Null; continue }
        $k = $raw.ToUpper()
        if ($k -match "^WAIT(\d+)$") { Start-Sleep -Milliseconds ([int]$Matches[1]); continue }
        Send-Key $k $hwnd
    }
}

# Launch -> main menu, without a human. The loader intro waits on the JOYSTICK fire
# button and nothing else (loader_intro.c, LoaderIntroWaitForFireOrTimeout reads
# _pl1Fire), so one FIRE inside the intro skips it; left alone the intro also runs out
# on its own after ~2 minutes, which is the fallback. At most one FIRE per attempt,
# because a FIRE that arrives when the main menu is ALREADY up opens FRIENDLY and
# derails every route after it.
function Wait-ForMainMenu([IntPtr]$hwnd, [int]$attempts = 3, [int]$pollSeconds = 18) {
    for ($a = 0; $a -lt $attempts; $a++) {
        if ($a -gt 0) { Write-Host "  no main menu yet -- FIRE again (attempt $($a + 1))" }
        Send-Key "FIRE" $hwnd
        $deadline = (Get-Date).AddSeconds($pollSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 1500
            if (-not (Get-Emu)) { throw "fs-uae exited while waiting for the main menu." }
            $bmp = Grab $hwnd
            $hit = Test-MainMenu $bmp
            $bmp.Dispose()
            if ($hit) { Write-Host "main menu reached"; return $true }
        }
    }
    # Last resort: the intro times out by itself (~2 min of loader frames).
    Write-Host "  still not there -- waiting the intro out (up to 150 s)"
    $deadline = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        if (-not (Get-Emu)) { throw "fs-uae exited while waiting for the main menu." }
        $bmp = Grab $hwnd
        $hit = Test-MainMenu $bmp
        $bmp.Dispose()
        if ($hit) { Write-Host "main menu reached (intro timed out)"; return $true }
    }
    return $false
}

function Clear-Diags {
    foreach ($f in $DiagFiles) {
        $p = Join-Path $Bin $f
        if (Test-Path $p) { Remove-Item $p -Force }
    }
}

function Collect-Diags([string]$label) {
    $dst = Join-Path $ShotDir ("diags" + $(if ($label) { "_$label" } else { "" }))
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    foreach ($f in $DiagFiles) {
        $p = Join-Path $Bin $f
        if (Test-Path $p) {
            Copy-Item $p $dst -Force
            $bytes = [System.IO.File]::ReadAllBytes($p)
            $hex = ($bytes[0..([math]::Min(31, $bytes.Length - 1))] | ForEach-Object { "{0:x2}" -f $_ }) -join " "
            Write-Host ("diag {0,-12} {1,5} bytes  {2}" -f $f, $bytes.Length, $hex)
        } else {
            Write-Host ("diag {0,-12} (absent)" -f $f)
        }
    }
    Write-Host "diags -> $dst"
}

# ---------------------------------------------------------------- comparing
#
# Screenshots are the Amiga build's only output, so "did this change anything?" has to
# be answered by comparing them -- by the script, not by eye. Two rules make the
# comparison mean something:
#
#  * Compare the EMULATED SCREEN ONLY. FSEMU's title bar and its two LED panels are
#    around the picture, and the VSYNC/POWER LEDs change colour from frame to frame --
#    compare the whole window and every shot "differs".
#  * Allow for the scaler. FS-UAE scales the Amiga frame with interpolation, so the
#    same game screen can land a shade apart; a pixel counts as changed only when a
#    channel moves by more than TOL.

function Get-Samples([System.Drawing.Bitmap]$bmp, [int]$step = 4) {
    $r = Get-EmuRect $bmp
    $ex = $r[0]; $ty = $r[1]; $ew = $r[2]; $eh = $r[3]
    # LockBits: GetPixel over a million pixels takes seconds, this takes milliseconds.
    $rect = New-Object System.Drawing.Rectangle $ex, $ty, $ew, $eh
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $data.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $out = New-Object System.Collections.Generic.List[byte]
    for ($y = 0; $y -lt $eh; $y += $step) {
        $row = $y * $data.Stride
        for ($x = 0; $x -lt $ew; $x += $step) {
            $i = $row + $x * 4
            $out.Add($bytes[$i]); $out.Add($bytes[$i + 1]); $out.Add($bytes[$i + 2])
        }
    }
    return ,$out.ToArray()
}

function Compare-Shot([string]$a, [string]$b, [int]$tol = 24) {
    $ba = [System.Drawing.Bitmap]::FromFile((Resolve-Path $a))
    $bb = [System.Drawing.Bitmap]::FromFile((Resolve-Path $b))
    try {
        if ($ba.Width -ne $bb.Width -or $ba.Height -ne $bb.Height) { return -1.0 }
        $sa = Get-Samples $ba
        $sb = Get-Samples $bb
    } finally { $ba.Dispose(); $bb.Dispose() }
    if ($sa.Length -ne $sb.Length) { return -1.0 }
    $diff = 0; $n = 0
    for ($i = 0; $i -lt $sa.Length; $i += 3) {
        $n++
        if ([math]::Abs($sa[$i] - $sb[$i]) -gt $tol -or
            [math]::Abs($sa[$i + 1] - $sb[$i + 1]) -gt $tol -or
            [math]::Abs($sa[$i + 2] - $sb[$i + 2]) -gt $tol) { $diff++ }
    }
    if ($n -eq 0) { return -1.0 }
    return (100.0 * $diff / $n)
}

# Shots are paired by LABEL (the part after the leading number), so a baseline still
# matches when a route gains or loses a shot ahead of the one you care about.
function Get-ShotLabel([string]$name) {
    if ($name -match "^\d+_(.+)$") { return $Matches[1] }
    return $name
}

function Invoke-Compare([string]$baseline) {
    if (-not (Test-Path $baseline)) { Write-Host "baseline not found: $baseline"; return 2 }
    if ((Get-Item $baseline).PSIsContainer) {
        $mine = @(Get-ChildItem (Join-Path $ShotDir "*.png") -ErrorAction SilentlyContinue | Sort-Object Name)
        $base = @(Get-ChildItem (Join-Path $baseline "*.png") -ErrorAction SilentlyContinue | Sort-Object Name)
    } else {
        $mine = @(Get-Item (Join-Path $ShotDir (Split-Path $baseline -Leaf)) -ErrorAction SilentlyContinue)
        $base = @(Get-Item $baseline)
        if (-not $mine) { Write-Host "no shot to compare against $baseline"; return 2 }
    }
    $index = @{}
    foreach ($b in $base) { $index[(Get-ShotLabel $b.BaseName)] = $b.FullName }
    $fail = 0; $seen = @{}
    Write-Host "--- compare $ShotDir  vs  $baseline"
    foreach ($m in $mine) {
        $label = Get-ShotLabel $m.BaseName
        if (-not $index.ContainsKey($label)) {
            Write-Host ("  {0,-26} NEW        (no baseline)" -f $label); $fail++; continue
        }
        $seen[$label] = $true
        $pct = Compare-Shot $m.FullName $index[$label]
        if ($pct -lt 0) {
            Write-Host ("  {0,-26} SIZE       (different dimensions)" -f $label); $fail++
        } elseif ($pct -gt $DiffPct) {
            Write-Host ("  {0,-26} DIFFERS    {1,6:n2}% of pixels" -f $label, $pct); $fail++
        } else {
            Write-Host ("  {0,-26} same       {1,6:n2}%" -f $label, $pct)
        }
    }
    foreach ($label in $index.Keys) {
        if (-not $seen.ContainsKey($label)) {
            Write-Host ("  {0,-26} MISSING    (baseline has it, this run does not)" -f $label); $fail++
        }
    }
    if ($fail -eq 0) { Write-Host "--- compare: all shots match the baseline" }
    else { Write-Host "--- compare: $fail shot(s) differ" }
    return $fail
}

function Save-Baseline([string]$dir) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Get-ChildItem (Join-Path $dir "*.png") -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem (Join-Path $ShotDir "*.png") -ErrorAction SilentlyContinue | Copy-Item -Destination $dir -Force
    Write-Host "baseline saved: $dir"
}

# ---------------------------------------------------------------- what is on screen
#
# Screenshots are the Amiga build's only output, so the harness has to be able to say
# "that is a pitch" / "that is black" / "that is the main menu" by itself -- otherwise
# every run still needs a human to look at the PNGs. These are deliberately coarse,
# structural features (how much of the screen is pitch green, how much is black, is
# there any white line art) rather than exact colours: FS-UAE scales the Amiga frame
# with interpolation, so nothing matches a reference pixel for pixel.

function Get-ScreenFeatures([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $path))
    try {
        $r = Get-EmuRect $bmp
        $rect = New-Object System.Drawing.Rectangle $r[0], $r[1], $r[2], $r[3]
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                              [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $bytes = New-Object byte[] ($data.Stride * $data.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        $bmp.UnlockBits($data)
        $isMenu = Test-MainMenu $bmp
    } finally { $bmp.Dispose() }

    $step = 4
    $black = 0; $green = 0; $white = 0; $noise = 0; $red = 0; $blue = 0; $n = 0; $prev = -1
    $sr = 0; $sg = 0; $sb = 0
    for ($y = 0; $y -lt $r[3]; $y += $step) {
        $row = $y * $data.Stride
        $prev = -1
        for ($x = 0; $x -lt $r[2]; $x += $step) {
            $i = $row + $x * 4
            $b = $bytes[$i]; $g = $bytes[$i + 1]; $rd = $bytes[$i + 2]
            $sr += $rd; $sg += $g; $sb += $b
            $lum = ($rd + $g + $b) / 3
            if ($lum -lt 20) { $black++ }
            # the pitch: green-dominant and not dark
            if ($g -gt $rd + 25 -and $g -gt $b + 40 -and $g -gt 60) { $green++ }
            # pitch markings, menu text, the white frame around a selected entry
            if ($rd -gt 180 -and $g -gt 180 -and $b -gt 180) { $white++ }
            # the team-selection colours: COMPUTER is pure red, PLAYER-COACH pure blue
            if ($rd -gt 150 -and $g -lt 80 -and $b -lt 80) { $red++ }
            if ($b -gt 150 -and $rd -lt 90 -and $g -lt 110) { $blue++ }
            if ($prev -ge 0 -and [math]::Abs($lum - $prev) -gt 60) { $noise++ }
            $prev = $lum
            $n++
        }
    }
    if ($n -eq 0) { $n = 1 }
    return [pscustomobject]@{
        BlackPct = 100.0 * $black / $n
        GreenPct = 100.0 * $green / $n
        WhitePct = 100.0 * $white / $n
        NoisePct = 100.0 * $noise / $n
        RedPct   = 100.0 * $red / $n
        BluePct  = 100.0 * $blue / $n
        Mean     = "{0:n0},{1:n0},{2:n0}" -f ($sr / $n), ($sg / $n), ($sb / $n)
        IsMenu   = $isMenu
    }
}

# The verdict. BLACK is the one that matters most: it is what a match that never drew
# looks like, and it is indistinguishable from "fine" in a run nobody watches.
function Get-ScreenKind($f) {
    if ($f.BlackPct -gt 95) { return "BLACK" }
    if ($f.IsMenu) { return "MAINMENU" }
    if ($f.GreenPct -gt 35 -and $f.WhitePct -gt 0.05) { return "PITCH" }
    if ($f.GreenPct -gt 35) { return "GREEN-NOLINES" }
    return "OTHER"
}

# label -> what that shot is supposed to be. ANY means "whatever it is, it is not the
# step being checked" (a fade, a transition).
function Invoke-Classify([string]$dir, [hashtable]$expect) {
    $shots = @(Get-ChildItem (Join-Path $dir "*.png") -ErrorAction SilentlyContinue | Sort-Object Name)
    if (-not $shots) { Write-Host "no shots in $dir"; return 2 }
    $fail = 0
    Write-Host "--- screen check ($dir)"
    Write-Host ("  {0,-26} {1,-14} {2,-14} {3}" -f "shot", "is", "expected", "black/green/white/noise %")
    foreach ($shot in $shots) {
        $label = Get-ShotLabel $shot.BaseName
        $f = Get-ScreenFeatures $shot.FullName
        $kind = Get-ScreenKind $f
        $want = "ANY"
        if ($expect -and $expect.ContainsKey($label)) { $want = $expect[$label] }
        $verdict = "  "
        if ($want -eq "REDBLUE") {
            # not a kind: "one side red (COMPUTER), the other blue (PLAYER-COACH)"
            if ($f.RedPct -gt 0.1 -and $f.BluePct -gt 0.1) { $verdict = "ok" }
            else { $verdict = "FAIL (red $('{0:n2}' -f $f.RedPct)% blue $('{0:n2}' -f $f.BluePct)%)"; $fail++ }
        } elseif ($want -ne "ANY") {
            if ($kind -eq $want) { $verdict = "ok" } else { $verdict = "FAIL"; $fail++ }
        }
        Write-Host ("  {0,-26} {1,-14} {2,-14} {3,5:n1} {4,5:n1} {5,5:n1} {6,5:n1}  {7}" -f `
            $label, $kind, $want, $f.BlackPct, $f.GreenPct, $f.WhitePct, $f.NoisePct, $verdict)
    }
    if ($fail -eq 0) { Write-Host "--- screen check: every shot is what it should be" }
    else { Write-Host "--- screen check: $fail shot(s) are NOT what they should be" }
    return $fail
}

# ---------------------------------------------------------------- routes
#
# Every route starts on the main menu with the cursor on its DEFAULT entry (FRIENDLY),
# which is what a fresh boot gives. Driving an instance that is already somewhere else
# (-NoLaunch) is only safe if you have put it back on the main menu yourself.
#
# Navigate by what the screen shows, not by counting fire presses: every step below was
# derived from a screenshot, and the shots a route leaves behind are how you check it
# still lands where it used to.

# What each route's shots must show for the run to count as a success. Anything not
# listed is ANY -- a fade or a transition proves nothing either way.
$script:ExpectFriendly = @{
    "main_menu_start" = "MAINMENU"
    "teams_picked"    = "REDBLUE"     # one side blue (human), the other red (computer)
    "match"           = "PITCH"       # a live match: pitch green with line markings
    "main_menu"       = "MAINMENU"    # and back out again
}
$script:ExpectChallenge = @{
    "main_menu_start" = "MAINMENU"
    "challenge_match" = "PITCH"
    "main_menu"       = "MAINMENU"
}

function Route-Friendly([IntPtr]$hwnd) {
    Write-Host "--- route friendly"
    # FRIENDLY -> OK -> CHOOSE TEAMS -> CLUB TEAMS -> EUROPE -> GERMANY -> 1. LIGA.
    # Each of those screens opens with its cursor on the entry we want, so this stretch
    # is a straight run of fire presses.
    Send-Keys $hwnd "FIRE,WAIT1800,SHOT:friendly_menu,FIRE,WAIT1800,FIRE,WAIT2500,SHOT:choose_teams"
    Send-Keys $hwnd "FIRE,WAIT2000,FIRE,WAIT2000,FIRE,WAIT2000,FIRE,WAIT2500,SHOT:team_list"
    # Control type cycles NOT_SELECTED -> COMPUTER -> PLAYER-COACH -> COACH (teams.c):
    # two presses make the team under the cursor a blue PLAYER-COACH (the human side),
    # one press on its neighbour makes that a red COMPUTER.
    Send-Keys $hwnd "FIRE,WAIT700,FIRE,WAIT900,LEFT,WAIT500,FIRE,WAIT1200,SHOT:teams_picked"
    # Down out of the 6-row team grid, then right twice: VIEW SELECTED TEAMS, EXIT,
    # PLAY FRIENDLY.
    Send-Keys $hwnd "DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,RIGHT,RIGHT,WAIT700,SHOT:play_button,FIRE,WAIT9000,SHOT:lineup"
    # PLAY/COACH MATCH -> kick-off.
    Send-Keys $hwnd "FIRE,WAIT10000,SHOT:kickoff,WAIT5000,SHOT:match"
}

function Route-Challenge([IntPtr]$hwnd) {
    Write-Host "--- route challenge"
    # CHALLENGES is the left column's bottom row; the cursor starts on FRIENDLY (right
    # column, top), so: left into EDIT, then four rows down.
    Send-Keys $hwnd "LEFT,DOWN,DOWN,DOWN,DOWN,WAIT700,FIRE,WAIT3000,SHOT:challenge_picker"
    # The picker lists the .CHL files in bin/save -- the first one is under the cursor.
    Send-Keys $hwnd "FIRE,WAIT7000,SHOT:challenge_teams,WAIT8000,SHOT:challenge_match"
}

function Route-Exit([IntPtr]$hwnd) {
    # ESC abandons the match and gives REPLAY / EXIT; EXIT (one row down) returns to
    # the main menu.
    Write-Host "--- back to the main menu"
    Send-Keys $hwnd "ESC,WAIT3000,SHOT:post_match,DOWN,WAIT500,FIRE,WAIT5000,SHOT:main_menu"
}

# ---------------------------------------------------------------- actions

if ($Shutdown) { Stop-Emu; Write-Host "fs-uae stopped"; exit 0 }

if ($Classify) {
    $expect = switch ($Route) {
        "friendly"  { $script:ExpectFriendly }
        "challenge" { $script:ExpectChallenge }
        default     { $null }
    }
    exit (Invoke-Classify $Classify $expect)
}

if ($Compare -and -not ($Route -or $Keys -or $Launch -or $Shot -or $WaitMenu -or $SaveBaseline)) {
    exit (Invoke-Compare $Compare)      # compare two directories of shots, emulator or not
}

if ($CollectDiags -and -not ($Route -or $Keys -or $Launch -or $Shot -or $WaitMenu)) {
    Collect-Diags $Tag
    exit 0
}

if ($Route) {
    if (-not $KeepShots) {
        Get-ChildItem (Join-Path $ShotDir "*.png") -ErrorAction SilentlyContinue | Remove-Item -Force
    }
    if (-not $NoLaunch) { Clear-Diags; Start-Emu }
    $hwnd = Get-Window
    $ok = [AmigaDrv]::Focus($hwnd)
    Write-Host "window   : '$([AmigaDrv]::TitleOf($hwnd))' focus ok=$ok"
    if (-not $ok) { Write-Host "!! no focus -- keys would go to '$([AmigaDrv]::FgTitle())'" }
    if (-not $NoLaunch) {
        $wait = $BootWaitMs - 2500
        if ($wait -gt 0) { Start-Sleep -Milliseconds $wait }
        if (-not (Wait-ForMainMenu $hwnd)) { throw "never reached the main menu -- see $ShotDir" }
    } elseif ($WaitMenu) {
        if (-not (Wait-ForMainMenu $hwnd)) { throw "never reached the main menu -- see $ShotDir" }
    }
    Take-Shot $hwnd "main_menu_start" | Out-Null
    switch ($Route) {
        "friendly"  { Route-Friendly  $hwnd }
        "challenge" { Route-Challenge $hwnd }
    }
    if (-not $Stay) { Route-Exit $hwnd }
    Collect-Diags $Route
    Write-Host "--- route $Route done. shots in $ShotDir"
    $expect = switch ($Route) {
        "friendly"  { $script:ExpectFriendly }
        "challenge" { $script:ExpectChallenge }
    }
    $bad = Invoke-Classify $ShotDir $expect
    if ($SaveBaseline) { Save-Baseline $SaveBaseline }
    if ($Compare) { $bad += Invoke-Compare $Compare }
    # The verdict only decides the exit code when asked: a route can land exactly where
    # it should and still FAIL here because the GAME drew the wrong thing, and those are
    # different questions.
    if ($Check) { exit $bad }
    exit 0
}

if ($Launch) { Clear-Diags; Start-Emu }

$hwnd = Get-Window
$ok = [AmigaDrv]::Focus($hwnd)
Write-Host "window   : '$([AmigaDrv]::TitleOf($hwnd))' focus ok=$ok (foreground: '$([AmigaDrv]::FgTitle())')"

if ($Launch) {
    $wait = $BootWaitMs - 2500
    if ($wait -gt 0) { Start-Sleep -Milliseconds $wait }
}
if ($WaitMenu) {
    if (-not (Wait-ForMainMenu $hwnd)) { Write-Host "!! never reached the main menu" }
}
if ($Keys) { Send-Keys $hwnd $Keys }
if ($Shot) { Take-Shot $hwnd $Tag | Out-Null }
if ($CollectDiags) { Collect-Diags $Tag }
if ($SaveBaseline) { Save-Baseline $SaveBaseline }
if ($Compare) { $rc = Invoke-Compare $Compare }
Write-Host "done. fs-uae alive=$([bool](Get-Emu))"
if ($Compare) { exit $rc }
