#define AppName "HoverNet"
#define AppVersion GetEnv("HOVERNET_VERSION")
#define SourceDir GetEnv("HOVERNET_X64_RELEASE_DIR")
#define OutputDir AddBackslash(GetEnv("CI_PROJECT_DIR")) + "dist"

[Setup]
AppId={{D78CDBB5-8B97-4D3E-A553-4444479C6C66}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\HoverNet
DefaultGroupName=HoverNet
UninstallDisplayName=HoverNet (64-bit)
OutputDir={#OutputDir}
OutputBaseFilename=HoverNet-{#AppVersion}-windows-x64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes

[Files]
Source: "{#SourceDir}\HoverNet.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\ObjFac1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\SDL2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall
Source: "{#SourceDir}\NetTarget\ObjFac1.dat"; DestDir: "{app}\NetTarget"; Flags: ignoreversion
Source: "{#SourceDir}\NetTarget\Tracks\*.trk"; DestDir: "{app}\NetTarget\Tracks"; Flags: ignoreversion
Source: "{#SourceDir}\NetTarget\CommunityTracks.tsv"; DestDir: "{app}\NetTarget"; Flags: ignoreversion
Source: "{#SourceDir}\NetTarget\LinuxClient\assets\menu-hovercraft.bmp"; DestDir: "{app}\NetTarget\LinuxClient\assets"; Flags: ignoreversion

[Icons]
Name: "{group}\HoverNet"; Filename: "{app}\HoverNet.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\HoverNet"; Filename: "{app}\HoverNet.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Flags: runhidden waituntilterminated
Filename: "{app}\HoverNet.exe"; Description: "Launch HoverNet"; Flags: nowait postinstall skipifsilent
