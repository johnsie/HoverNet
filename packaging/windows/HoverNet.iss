#define AppName "HoverNet"
#define AppVersion GetEnv("HOVERNET_VERSION")
#define SourceDir GetEnv("HOVERNET_RELEASE_DIR")
#define OutputDir AddBackslash(GetEnv("CI_PROJECT_DIR")) + "dist"

[Setup]
AppId={{D78CDBB5-8B97-4D3E-A553-4444479C6C66}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\HoverNet
DefaultGroupName=HoverNet
UninstallDisplayName=HoverNet
OutputDir={#OutputDir}
OutputBaseFilename=HoverNet-{#AppVersion}-win32-setup
Compression=lzma2
SolidCompression=yes

[Files]
Source: "{#SourceDir}\Game2.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\ObjFac1.dat"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\tracks\*"; DestDir: "{app}\tracks"; Flags: ignoreversion

[Icons]
Name: "{group}\HoverNet"; Filename: "{app}\Game2.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\HoverNet"; Filename: "{app}\Game2.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{app}\Game2.exe"; Description: "Launch HoverNet"; Flags: nowait postinstall skipifsilent