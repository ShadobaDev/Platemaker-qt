#define MyAppName "Platemaker"
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#define MyAppPublisher "Bartłomiej Mucha"
#define MyAppExeName "Platemaker.exe"
#define MyInstallDir "install"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir=installer-output
OutputBaseFilename=Platemaker-{#MyAppVersion}-Setup
SetupIconFile=icons\icon-blue.ico
UninstallDisplayIcon={app}\bin\{#MyAppExeName}
LicenseFile=LICENSE
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
; Restart Manager closes a running instance before its files are replaced. Do not add a
; taskkill /F here: it is redundant, it kills the process with no chance to save, and an
; installer spawning taskkill is a pattern AV heuristics react to.
CloseApplications=yes
CloseApplicationsFilter=*Platemaker.exe
RestartApplications=no

[Languages]
; Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Main executable - restartreplace in case the file is locked (e.g. held by AV)
Source: "{#MyInstallDir}\bin\{#MyAppExeName}"; DestDir: "{app}\bin"; Flags: ignoreversion restartreplace uninsrestartdelete
; Everything else in bin\
Source: "{#MyInstallDir}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs

; Qt plugins
Source: "{#MyInstallDir}\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs

; Qt translations
Source: "{#MyInstallDir}\translations\*"; DestDir: "{app}\translations"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; IconFilename: "{app}\bin\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
