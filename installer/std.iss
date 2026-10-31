; STD — Inno Setup installer for the Qt 6.11.1 MSVC build.
;
; Same shape as Lyra (installer/lyra.iss) and SDRLogger+ (SDRLoggerPlus.iss):
; one Windows setup exe, Start Menu and desktop shortcuts, modern wizard.
; Packages the windeployqt tree next to std.exe. The purple capsule is
; resources/std.ico, used for Setup, the Start Menu, and the desktop shortcut.
;
; Build:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\std.iss
; Output: dist\STD-Setup-0.1.1.exe
;
; Keep AppVersion in sync with CMake project(VERSION) / STD_VERSION.

#define AppName      "STD"
#define AppVersion   "0.1.1"
#define AppPublisher "Rick Langford (N8SDR)"
#define AppURL       "https://github.com/N8SDR1/STD"
#define AppExe       "std.exe"
#define BuildDir     "..\build"
#define IcoFile      "..\resources\std.ico"

[Setup]
; Stable AppId. A later setup with this same id installs over the existing
; folder (Program Files\STD, or wherever it was first installed). The
; operator does not uninstall first. Do not change this id.
AppId={{C4A91E70-6B2D-4F18-9A33-8E5D1C0B7A64}
UsePreviousAppDir=yes
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\STD
DefaultGroupName=STD
DisableProgramGroupPage=yes
AllowNoIcons=no
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=STD-Setup-{#AppVersion}
SetupIconFile={#IcoFile}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[InstallDelete]
Type: filesandordirs; Name: "{app}\*"

[Files]
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "test_*.exe,genprobe.exe,*.lib,*.pdb,*.exp,*.ilk,*.cmake,*.ninja,*.obj,*.o,*.txt,*.json,CMakeCache.txt,.ninja_deps,.ninja_log,CMakeFiles\*,.qt\*,std_autogen\*,ft8obj\*,ft8_live.*,ft8_smoke.*,ft8_capture.*,ft8-capture.bin,jt65_catch.exe,moc_*.cpp,selftest*,_selftest*,profile-bar.png"
Source: "{#IcoFile}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\STD"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\std.ico"; Comment: "STD — Shift, Tone, Decode"
Name: "{group}\Uninstall STD"; Filename: "{uninstallexe}"
Name: "{autodesktop}\STD"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\std.ico"; Comment: "STD — Shift, Tone, Decode"; Tasks: desktopicon

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: waituntilterminated
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,STD}"; Flags: nowait postinstall skipifsilent

[Code]
procedure InitializeWizard();
begin
  WizardForm.WelcomeLabel2.Caption :=
    'This will install STD ' + '{#AppVersion}' + ' on your computer.' + #13#10 + #13#10 +
    'STD is Shift, Tone, Decode, a digimode console for a TCI radio.' + #13#10 +
    'testing phase - highly transmissible.' + #13#10 + #13#10 +
    'Connect sends AUDIO_START for receiver 0. The text command START is never sent. CW stays in Lyra.' + #13#10 + #13#10 +
    'If STD is already installed, this setup replaces that copy in the same folder. You do not uninstall first.' + #13#10 + #13#10 +
    'Click Next to continue, or Cancel to exit Setup.';
end;
