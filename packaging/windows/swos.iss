; swos.iss -- Inno Setup script for the Windows installer, dist\SWOS-Setup.exe.
;
; It packages a STAGED directory (dist\windows) rather than bin\ itself.
; bin\ is the shared data directory for every target in this tree: it holds the
; Linux ELFs, the m68k SWOS2, the WASM bundle, 24 MB of test careers in save\,
; the release zips, FS-UAE .uaem sidecars and SWOS.log. Sweeping it with a
; wildcard would ship all of that. scripts\build_installer.sh copies exactly the
; runtime set into dist\windows first -- one reviewable list, in one place --
; and this script sweeps the stage.
;
;     scripts/build_installer.sh                 stage + compile (the normal way)
;     "C:\Program Files\Inno Setup 7\ISCC.exe" packaging\windows\swos.iss
;
; Running ISCC by hand packages WHATEVER WAS LAST STAGED, so a rebuilt SWOS.exe
; that has not been re-staged is silently left out of an installer that looks new.
; Use the script.
;
; Saves are neither installed nor uninstalled: the game keeps them under
; %USERPROFILE%\Documents\SWOS (swos_io.c), so reinstalling never costs a player
; their career and uninstalling leaves it for next time.

; Overridable from the command line with ISCC /DAppVersion=1.0.1 -- that is how
; scripts/build_installer.sh --version works.
#ifndef AppVersion
  #define AppVersion   "1.0.0"
#endif

#define AppName        "OpenSWOS"
#define AppPublisher   "Baker76"
#define AppExeName     "SWOS.exe"
; The data setup: OpenSWOS ships none of the original game's data, and this makes
; it from the player's own copy (tools/swosdata). Run when the install finishes.
#define SetupExeName   "openswos-setup.exe"
#define SetupName      AppName + " Setup"
#define AppURL         "https://github.com/benbaker76/OpenSWOS"

; Relative to THIS file. Inno resolves relative paths against the script's
; directory, not the current one, so these hold wherever ISCC is invoked from.
; dist\windows is the staging tree build_installer.sh fills; dist\ itself is
; where every release artifact lands, so the installer sits beside the zips.
#define StageDir       "..\..\dist\windows"
#define RootDir        "..\.."
#define OutputDir      "..\..\dist"

[Setup]
; Never reuse this GUID for another product -- it is what tells Windows an
; install is an upgrade of OpenSWOS rather than a second copy of it.
AppId={{5E12A462-389E-5F60-9DC6-54B753E848D0}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}
; Windows 10 or later: the game's Cygwin (3.5+) dropped 7 and 8, and the setup window's
; Slint is Rust, whose programs need 10. On 7 the setup crashed at start (0xc0000005);
; this makes the installer say so instead.
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=SWOS-Setup
SetupIconFile={#RootDir}\SWOS.ico
WizardStyle=modern
; The two wizard graphics, in this directory. Inno wants BMP, not PNG, and
; scales them to the wizard's DPI:
;   WizardImage.bmp        164 x 314 -- the tall banner down the left of the
;                                       welcome and finished pages
;   WizardSmallImage.bmp    55 x  58 -- the badge in the top-right of every
;                                       other page
; Both 24-bit uncompressed. Inno also accepts a comma-separated list of
; higher-resolution variants (WizardImage.bmp,WizardImage125.bmp,...) if crisper
; images at 125%/150% DPI are ever wanted.
WizardImageFile=WizardImage.bmp
WizardSmallImageFile=WizardSmallImage.bmp
; ~50 MB staged, and most of it is highly compressible: bin\data_json is 20 MB
; of JSON and the grafs tiers are raw indexed pixels. Unlike a project shipping
; PNG/OGG, max pays for itself here -- it is the difference between a download a
; tester waits on and one they do not.
Compression=lzma2/max
SolidCompression=yes
; bin\SWOS.exe is PE32+ x86-64. There is no 32-bit Windows build in this tree.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Admin by default so it lands in Program Files for every account, but the
; wizard offers a per-user install that needs no elevation -- which matters on a
; machine the player does not administer. Saves are per-user either way.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; One sweep of the stage. recursesubdirs picks up data\, grafs*\, sound\ and
; data_json\; SWOS.exe and the two Cygwin DLLs beside it come along as ordinary
; files. See scripts/build_installer.sh for what the stage contains and why.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{#SetupName}"; Filename: "{app}\{#SetupExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
; Not the game: without the player's data it has nothing to show. Two boxes on the last page:
;  - the data made with no window (--headless, tools/swosdata/app/main.cpp App::Headless):
;    nothing when it is up to date; an update from the sources the last install used when a
;    part is out of date (an upgrade); else from the copy of the game found where the window
;    looks at its start (GOG, the installed SWOS programs, the CD, Downloads), else on every
;    drive (a small window with Stop). The setup's window opens by itself only when no copy is
;    found, the search is stopped or making the data fails. Waited for,
;    as the player who started the installer (the data goes to THEIR Documents, not the
;    admin's), and not hidden: that window must be able to show.
;  - the setup's whole window, for a player who wants to choose (unticked).
; The automatic pass also starts the game when there is enough to play (so there is no "Launch
; the game" box of Inno's); with the window ticked too, it passes --no-play (HeadlessParams) and the
; window follows it.
Filename: "{app}\{#SetupExeName}"; Parameters: "{code:HeadlessParams}"; Description: "Make the game data automatically from your copy of Sensible World of Soccer"; StatusMsg: "Making the game data from your copy of Sensible World of Soccer..."; Flags: postinstall waituntilterminated runasoriginaluser skipifsilent
Filename: "{app}\{#SetupExeName}"; Description: "Open {#SetupName}"; Flags: postinstall nowait runasoriginaluser skipifsilent unchecked

[Code]
// The automatic entry's arguments: --no-play when "Open ... Setup" (the second of the last page's
// boxes) is ticked too -- the window comes next, and the game is played from there.
function HeadlessParams(Param: String): String;
begin
  Result := '--headless';
  if (WizardForm.RunList.Items.Count > 1) and WizardForm.RunList.Checked[1] then
    Result := Result + ' --no-play';
end;

[UninstallDelete]
; The data trees are installed wholesale by one wildcard, so remove them
; wholesale -- Inno only tracks files it laid down individually, and a directory
; it did not create by name is left behind with everything in it.
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\data_json"
Type: filesandordirs; Name: "{app}\grafs"
Type: filesandordirs; Name: "{app}\grafs_aga"
Type: filesandordirs; Name: "{app}\grafs_dos"
Type: filesandordirs; Name: "{app}\grafs_2020"
Type: filesandordirs; Name: "{app}\grafs_arcade"
Type: filesandordirs; Name: "{app}\sound"
Type: filesandordirs; Name: "{app}\sound_dos"
Type: filesandordirs; Name: "{app}\grafs_jag"
Type: filesandordirs; Name: "{app}\mods"
Type: filesandordirs; Name: "{app}\setup"
Type: filesandordirs; Name: "{app}\portmaster"
