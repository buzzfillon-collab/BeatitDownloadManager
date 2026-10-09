#define MyAppName "Beatit Download Manager"
#define MyAppVersion "0.1.0-beta1"
#define MyAppPublisher "Beatit"
#define MyAppExeName "BeatitDownloadManager.exe"
#define BuildDir "../build"

[Setup]
AppId={{B4B6D6E0-1F2C-4B0E-9B4B-9C5D6E7F8101}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\Beatit
DefaultGroupName={#MyAppName}
OutputDir=../installer-output
OutputBaseFilename=BeatitDownloadManager-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
DisableProgramGroupPage=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "browserextension"; Description: "Open the browser integration folder after installation"; Flags: unchecked

[Files]
Source: "{#BuildDir}/*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\magnet"; ValueType: string; ValueName: ""; ValueData: "URL:Magnet Protocol"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\magnet"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\magnet\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\magnet\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.torrent"; ValueType: string; ValueName: ""; ValueData: "BeatitTorrentFile"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\BeatitTorrentFile"; ValueType: string; ValueName: ""; ValueData: "BitTorrent file"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\BeatitTorrentFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\BeatitTorrentFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekeyifempty

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Beatit Download Manager"; Flags: nowait postinstall skipifsilent
Filename: "{sys}\explorer.exe"; Parameters: """{app}\browser\extension"""; Description: "Open browser integration folder"; Flags: nowait postinstall skipifsilent; Tasks: browserextension

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\Beatit\native-messaging-hosts"
