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
Name: "{group}\{#AppName} Setup"; Filename: "{app}\{#SetupExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
; Not the game: without the player's data it has nothing to show. The setup has a
; Play button for when it is done.
Filename: "{app}\{#SetupExeName}"; Description: "Set up the game data from your copy of Sensible World of Soccer"; Flags: nowait postinstall skipifsilent

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
