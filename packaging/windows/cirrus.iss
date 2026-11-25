; The Windows installer (packaging/README.md). Built by the release workflow:
;   iscc /DAppVersion=0.1.0 /DStageDir=<installed tree> /DOutputDir=<dir> packaging\windows\cirrus.iss
; Installs for the current user by default (no administrator rights); the first page
; offers installing for all users instead.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef StageDir
  #error Pass /DStageDir=<the cmake --install tree>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change AppId: Windows recognises upgrades and the uninstaller by it.
AppId={{6E0C2B7A-5D3F-4C1E-9A8B-2F4D7C9E1A35}
AppName=Cirrus
AppVersion={#AppVersion}
AppVerName=Cirrus {#AppVersion}
AppPublisher=guang233
AppCopyright=Cirrus is not affiliated with NetEase.
DefaultDirName={autopf}\Cirrus
DefaultGroupName=Cirrus
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=Cirrus-{#AppVersion}-windows-x64-setup
SetupIconFile={#SourcePath}\..\..\ui\app\icons\cirrus.ico
UninstallDisplayIcon={app}\Cirrus.exe
UninstallDisplayName=Cirrus
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
#if FileExists(CompilerPath + "Languages\ChineseSimplified.isl")
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#endif
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
#if FileExists(CompilerPath + "Languages\ChineseSimplified.isl")
zh.OpenLinks=打开网易云音乐网页里的“在客户端打开”链接（代替官方客户端）
#endif
en.OpenLinks=Open the NetEase Cloud Music web's "open in client" links (instead of the official client)

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
; orpheus:// links (docs/ui/pages/open-links.md): optional, the official client takes them too.
Name: "orpheuslinks"; Description: "{cm:OpenLinks}"; Flags: unchecked

[Registry]
; HKA: the user's classes for a per-user install, the machine's for all users. Removed with
; Cirrus, which brings back a registration the official client made in the other root.
Root: HKA; Subkey: "Software\Classes\orpheus"; ValueType: string; ValueName: ""; ValueData: "URL:orpheus"; Flags: uninsdeletekey; Tasks: orpheuslinks
Root: HKA; Subkey: "Software\Classes\orpheus"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Tasks: orpheuslinks
Root: HKA; Subkey: "Software\Classes\orpheus\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\Cirrus.exe"",0"; Tasks: orpheuslinks
Root: HKA; Subkey: "Software\Classes\orpheus\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Cirrus.exe"" ""%1"""; Tasks: orpheuslinks

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; An upgrade replaces the whole tree, so files a newer version dropped do not linger.
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\plugins"

[Icons]
Name: "{autoprograms}\Cirrus"; Filename: "{app}\Cirrus.exe"
Name: "{autodesktop}\Cirrus"; Filename: "{app}\Cirrus.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Cirrus.exe"; Description: "{cm:LaunchProgram,Cirrus}"; Flags: nowait postinstall skipifsilent
; The app's own update (docs/tasks/update-check.md §9) runs this silent with /RELAUNCH=1:
; Cirrus opens again once it is installed.
Filename: "{app}\Cirrus.exe"; Flags: nowait runasoriginaluser; Check: RelaunchAfterUpdate

[Code]
function RelaunchAfterUpdate: Boolean;
begin
  Result := WizardSilent and (ExpandConstant('{param:relaunch|0}') = '1');
end;
