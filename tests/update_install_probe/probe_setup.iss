; 探针用安装包：模拟官方 Inno setup，但用独立 AppId、免管理员权限，
; 只往 {app} 里放一个「新版本」替身 + 一份出厂数据目录。
#ifndef SourceDir
  #error "缺少 /DSourceDir="
#endif
#ifndef OutputDir
  #error "缺少 /DOutputDir="
#endif

[Setup]
AppId={{11111111-2222-3333-4444-555555555555}
AppName=BlockBox probe
AppVersion=9.9.9
DefaultDirName={autopf}\BlockBoxProbe
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=BlockBox-9.9.9-win64-setup
Compression=none
SolidCompression=no
Uninstallable=no
CreateUninstallRegKey=no

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs createallsubdirs

[Run]
Filename: "{app}\BlockBoxWindows.exe"; Flags: nowait postinstall skipifsilent
