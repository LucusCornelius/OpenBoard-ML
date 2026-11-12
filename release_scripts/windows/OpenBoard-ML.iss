; Inno Setup script of the OpenBoard-ML installer (Windows x64).
;
; It packs a complete deployed tree, made by release_scripts\windows\build.ps1: the program, Qt and
; Qt WebEngine (QtWebEngineProcess.exe, resources\, translations\qtwebengine_locales\), poppler,
; OpenSSL and their DLLs, the Microsoft C++ runtime DLLs (app-local, so no vc_redist is needed) and
; OpenBoard's data folders (etc, library, startupHints, customizations, fonts, i18n).
; Everything comes from that tree, so the installer and the portable zip have the same contents.
;
; build.ps1 calls it like this (Inno Setup 6.3 or newer):
;   iscc /DAppVersion=1.0.0 /DNumericVersion=1.0.0.0 /DSourceDir=<tree> /DOutputDir=<dir>
;        /DOutputBaseFilename=OpenBoard-ML-1.0.0-Windows-x64-Setup /DProjectRoot=<repo> OpenBoard-ML.iss
;
; (OpenBoard.iss next to this file is upstream's script for its own OpenBoard-ThirdParty layout.)

#if Ver < EncodeVer(6, 3, 0)
  #error Inno Setup 6.3 or newer is required
#endif

#ifndef AppVersion
  #error AppVersion is not defined: build the installer with release_scripts\windows\build.ps1
#endif
#ifndef SourceDir
  #error SourceDir (the deployed tree) is not defined
#endif
#ifndef ProjectRoot
  #define ProjectRoot AddBackslash(SourcePath) + "..\.."
#endif
#ifndef OutputDir
  #define OutputDir ProjectRoot + "\install\win32"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0.0"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "OpenBoard-ML-" + AppVersion + "-Windows-x64-Setup"
#endif

#if !FileExists(AddBackslash(SourceDir) + "OpenBoard-ML.exe")
  #error OpenBoard-ML.exe is not in SourceDir
#endif

[Setup]
; The AppId identifies the installation (upgrades, uninstall entry). OpenBoard-ML has its own, so
; it installs next to upstream OpenBoard instead of replacing it. Never change it.
AppId={{C31A600A-FED2-4529-99F0-B0ED4E4DF5D0}
AppName=OpenBoard-ML
AppVersion={#AppVersion}
AppVerName=OpenBoard-ML {#AppVersion}
AppPublisher=OpenBoard-ML
AppComments=Interactive whiteboard, based on OpenBoard
VersionInfoVersion={#NumericVersion}
VersionInfoProductName=OpenBoard-ML
VersionInfoDescription=OpenBoard-ML installer
UninstallDisplayName=OpenBoard-ML {#AppVersion}
UninstallDisplayIcon={app}\OpenBoard-ML.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Qt 6 needs Windows 10 1809 or newer
MinVersion=10.0.17763
DefaultDirName={autopf}\OpenBoard-ML
DefaultGroupName=OpenBoard-ML
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ChangesAssociations=yes
CloseApplications=yes
WizardStyle=modern
SetupIconFile={#ProjectRoot}\resources\win\OpenBoard.ico
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; files of an earlier version that the new tree may no longer have (Qt plugins, DLLs, data)
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\*.pdb"
Type: filesandordirs; Name: "{app}\customizations"
Type: filesandordirs; Name: "{app}\etc"
Type: filesandordirs; Name: "{app}\fonts"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\i18n"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\library"
Type: filesandordirs; Name: "{app}\multimedia"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\position"
Type: filesandordirs; Name: "{app}\resources"
Type: filesandordirs; Name: "{app}\startupHints"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\translations"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\OpenBoard-ML"; Filename: "{app}\OpenBoard-ML.exe"
Name: "{autodesktop}\OpenBoard-ML"; Filename: "{app}\OpenBoard-ML.exe"; Tasks: desktopicon

[Registry]
; .ubz documents open with OpenBoard-ML
Root: HKA; Subkey: "Software\Classes\.ubz"; ValueType: string; ValueName: ""; ValueData: "OpenBoardMLFile"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.ubz\OpenWithProgids"; ValueType: string; ValueName: "OpenBoardMLFile"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\OpenBoardMLFile"; ValueType: string; ValueName: ""; ValueData: "OpenBoard document"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\OpenBoardMLFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\OpenBoard-ML.exe,1"
Root: HKA; Subkey: "Software\Classes\OpenBoardMLFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\OpenBoard-ML.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\OpenBoard-ML.exe\SupportedTypes"; ValueType: string; ValueName: ".ubz"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\OpenBoard-ML.exe"; Description: "{cm:LaunchProgram,OpenBoard-ML}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; files the program or a later installer may have added to its own folder
Type: filesandordirs; Name: "{app}"
