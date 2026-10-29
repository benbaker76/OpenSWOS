# swos_drive.ps1 -- drive the Windows SDL build from a script, for UI testing.
#
# Injects real OS-level key events into the game window and lets the game take its own
# screenshots (F12 -> bin/swos_shotNNN.png), so a UI change can be verified without a
# human at the keyboard. See docs/UI_DRIVER.md.
#
#   scripts/swos_drive.ps1 -Launch -Keys "DOWN,Z,MENU:challengeMenu,SHOT"
#   scripts/swos_drive.ps1 -Keys "DOWN,DOWN,Z"
#   scripts/swos_drive.ps1 -Shutdown
#
# -Launch starts the game with --skip-intro and returns only once SWOS.log says the
# main menu is up ("[menu] show swosMainMenu"), so the first key lands on it. Fire
# pressed through the intro instead falls onto FRIENDLY. -Intro keeps the intro (for
# testing it); -GameArgs passes more arguments (with --menu= or --challenge= the game
# does not start at the main menu, so -Launch does not wait for it).
#
# Keys: UP DOWN LEFT RIGHT | FIRE/Z (fire) | X ENTER ESC SPACE | SHOT (F12 screenshot)
#       any bare letter/digit types that character (filename entry; Z is still fire)
#       WAIT<ms> pauses. -KeyDelayMs sets the gap between keys (default 350).
#       MENU:<name> waits until the latest [menu] line in SWOS.log names that menu or
#       screen (swosMainMenu, challengeMenu, challenge/list, swos2020/logInMenu ...),
#       and stops the run if it does not within -MenuTimeoutMs (default 8000).
#       The [menu] lines need a tester build or SWOS_MENU_NAV_LOG=1.
#
# Two things make it work, and both are easy to break:
#  * Focus. SetForegroundWindow from a background process is blocked by Windows, so
#    Focus() uses the AttachThreadInput dance. Without focus the keys go nowhere and
#    the game looks "unresponsive" -- check the "focus ok=True" line it prints.
#  * Scancodes. SendInput uses KEYEVENTF_SCANCODE; SDL reads scancodes, and PostMessage
#    does not reach it.
param(
    [string]$Keys = "",
    [switch]$Launch,
    [switch]$Shutdown,
    [switch]$Intro,
    [string[]]$GameArgs = @(),
    [int]$KeyDelayMs = 350,
    [int]$MenuTimeoutMs = 8000
)
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$GameDir  = Join-Path $RepoRoot "bin"
$LogPath  = Join-Path $env:USERPROFILE "Documents\SWOS\SWOS.log"
# The runtime DLLs must be reachable: a Cygwin build needs C:\cygwin64\bin
# (cygwin1.dll + cygSDL2/cygSDL3), a MinGW build needs SDL2.dll beside the exe
# or MSYS2's bin on PATH. Both are prepended when present; harmless if absent.
$DllDirs  = @("C:\cygwin64\bin", "C:\msys64\mingw64\bin")

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class SwosDrv {
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit, Size=40)]
    public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }

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
    public delegate bool EnumProc(IntPtr h, IntPtr p);

    /* The Cygwin build also owns a console window, and Process.MainWindowHandle can
     * return that one -- keys then go to the console and the game looks dead. Find the
     * SDL window by its title instead (main.c sets "OpenSWOS <hash> ..."). */
    public static IntPtr FindGameWindow(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint wpid; GetWindowThreadProcessId(h, out wpid);
            if (wpid != pid || !IsWindowVisible(h)) return true;
            StringBuilder sb = new StringBuilder(300);
            GetWindowText(h, sb, 300);
            if (sb.ToString().StartsWith("OpenSWOS")) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static bool Focus(IntPtr hwnd) {
        ShowWindow(hwnd, 9);               // SW_RESTORE
        BringWindowToTop(hwnd);
        SetWindowPos(hwnd, new IntPtr(-1), 0,0,0,0, 0x0001|0x0002|0x0040); // TOPMOST, NOSIZE|NOMOVE|SHOWWINDOW
        SetWindowPos(hwnd, new IntPtr(-2), 0,0,0,0, 0x0001|0x0002|0x0040); // NOTOPMOST

        IntPtr fg = GetForegroundWindow();
        uint fgThread = GetWindowThreadProcessId(fg, out _);
        uint myThread = GetCurrentThreadId();
        uint tgtThread = GetWindowThreadProcessId(hwnd, out _);

        AttachThreadInput(myThread, fgThread, true);
        AttachThreadInput(myThread, tgtThread, true);
        bool ok = SetForegroundWindow(hwnd);
        AttachThreadInput(myThread, tgtThread, false);
        AttachThreadInput(myThread, fgThread, false);

        System.Threading.Thread.Sleep(250);
        return GetForegroundWindow() == hwnd;
    }

    public static string FgTitle() {
        StringBuilder sb = new StringBuilder(300);
        GetWindowText(GetForegroundWindow(), sb, 300);
        return sb.ToString();
    }


    public static void Tap(ushort vk, bool ext, int holdMs) {
        ushort sc = (ushort)MapVirtualKey(vk, 0);
        uint f = 0x0008u | (ext ? 0x0001u : 0u);   // SCANCODE | EXTENDEDKEY
        INPUT[] d = new INPUT[1]; d[0].type = 1; d[0].ki.wScan = sc; d[0].ki.dwFlags = f;
        SendInput(1, d, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(holdMs);
        INPUT[] u = new INPUT[1]; u[0].type = 1; u[0].ki.wScan = sc; u[0].ki.dwFlags = f | 0x0002u;
        SendInput(1, u, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@

$KeyMap = @{
    "UP"=@(0x26,$true); "DOWN"=@(0x28,$true); "LEFT"=@(0x25,$true); "RIGHT"=@(0x27,$true)
    "FIRE"=@(0x5A,$false); "Z"=@(0x5A,$false); "X"=@(0x58,$false)
    "ENTER"=@(0x0D,$false); "ESC"=@(0x1B,$false); "SPACE"=@(0x20,$false)
    "SHOT"=@(0x7B,$false); "F9"=@(0x78,$false)
}

function Get-GameProc { Get-Process SWOS -ErrorAction SilentlyContinue | Select-Object -First 1 }

# The menu or screen the latest "[menu] show|back <name> [(<screen>)] on #N" line names.
function Get-CurrentMenu {
    if (-not (Test-Path $LogPath)) { return $null }
    $lines = @(Get-Content $LogPath -ErrorAction SilentlyContinue |
               Where-Object { $_ -match '^\[menu\] (show|back) ' })
    if ($lines.Count -eq 0) { return $null }
    if ($lines[-1] -match '^\[menu\] (show|back) (\S+)(?: \((\S+)\))? on #(-?\d+)') {
        return [pscustomobject]@{ Menu = $Matches[2]; Screen = $Matches[3]; Entry = [int]$Matches[4] }
    }
    return $null
}

function Wait-Menu([string]$name, $proc) {
    $deadline = (Get-Date).AddMilliseconds($MenuTimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $m = Get-CurrentMenu
        if ($m -and ($m.Menu -eq $name -or $m.Screen -eq $name)) {
            Write-Host "on $($m.Menu)$(if ($m.Screen) { " ($($m.Screen))" }) #$($m.Entry)"
            return $true
        }
        if ($proc -and $proc.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    $m = Get-CurrentMenu
    Write-Host "!! not on $name after $MenuTimeoutMs ms; latest: $(if ($m) { "$($m.Menu) $($m.Screen) #$($m.Entry)" } else { 'no [menu] line in SWOS.log' })"
    return $false
}

if ($Shutdown) { Get-Process SWOS -ErrorAction SilentlyContinue | Stop-Process -Force; Write-Host "stopped"; exit 0 }

if ($Launch) {
    Get-Process SWOS -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
    if (Test-Path $LogPath) { Remove-Item $LogPath -Force }
    Get-ChildItem "$GameDir\swos_shot*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
    foreach ($d in $DllDirs) { if (Test-Path $d) { $env:PATH = "$d;" + $env:PATH } }
    $argList = @($GameArgs)
    if (-not $Intro) { $argList = @("--skip-intro") + $argList }
    if ($argList.Count -gt 0) {
        Start-Process -FilePath "$GameDir\SWOS.exe" -WorkingDirectory $GameDir -ArgumentList $argList
    } else {
        Start-Process -FilePath "$GameDir\SWOS.exe" -WorkingDirectory $GameDir
    }
    Start-Sleep -Milliseconds 500
}

$proc = Get-GameProc
if (-not $proc) { Write-Host "SWOS NOT RUNNING"; exit 1 }
# Without the intro the main menu is the first screen: wait for it rather than for a
# fixed time. (With -Intro the script cannot know when the intro ends; drive it. With
# --menu=/--challenge= the main menu is not first: wait with MENU: or WAIT.)
$startsElsewhere = @($GameArgs | Where-Object { $_ -match '^--(menu|challenge)=' }).Count -gt 0
if ($Launch -and -not $Intro -and -not $startsElsewhere -and -not (Wait-Menu "swosMainMenu" $proc)) { exit 1 }

$hwnd = [SwosDrv]::FindGameWindow([uint32]$proc.Id)
if ($hwnd -eq [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle }
$focused = [SwosDrv]::Focus($hwnd)
Write-Host "focus ok=$focused (foreground now: '$([SwosDrv]::FgTitle())')"

foreach ($k in ($Keys -split ",")) {
    $raw = $k.Trim()
    $k = $raw.ToUpper()
    if ($k -eq "") { continue }
    if ($proc.HasExited) { Write-Host "!! game exited (code $($proc.ExitCode)) before key '$k'"; break }
    if ($k -match "^WAIT(\d+)$") { Start-Sleep -Milliseconds ([int]$Matches[1]); continue }    if ($k -match "^MENU:") {   # menu names are case sensitive: the token as written
        if (-not (Wait-Menu $raw.Substring(5) $proc)) { exit 1 }
        continue
    }
    if ($KeyMap.ContainsKey($k)) {
        $vk,$ext = $KeyMap[$k]
    } elseif ($k -match "^[A-Z0-9]$") {
        # Bare letter/digit (filename entry): the VK code IS the character.
        $vk = [int][char]$k; $ext = $false
    } else { Write-Host "unknown key $k"; continue }
    [SwosDrv]::Tap([ushort]$vk, [bool]$ext, 80)
    Start-Sleep -Milliseconds $KeyDelayMs
}

$proc.Refresh()
Write-Host "--- keys done. alive=$(-not $proc.HasExited) ---"
