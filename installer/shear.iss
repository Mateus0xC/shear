; Instalador do shear (Inno Setup 6). Gere com scripts\make-installer.ps1.
#ifndef AppDir
  #define AppDir "..\build\src\app"
#endif
#ifndef VcRedist
  #define VcRedist ""
#endif
#define AppName "shear"
#define AppVersion "0.1.0"

[Setup]
AppId={{6E1B8F43-5A2C-4C57-9B0E-7D2F3A9C41E8}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=shear-setup
SetupIconFile=..\resources\shear.ico
UninstallDisplayIcon={app}\shear.exe
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
; Sempre pergunta a pasta de instalação (mesmo numa reinstalação) e sempre começa com as caixas marcadas como o padrão:
; sem isto o Inno repete as escolhas da instalação anterior (e uma caixa nova, como a do menu, aparecia desmarcada).
DisableDirPage=no
UsePreviousTasks=no

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na área de trabalho"; Flags: unchecked
Name: "contextmenu"; Description: "Mostrar “Cortar com shear” ao clicar com o botão direito em um vídeo"

[Files]
; Ficam de fora: "plugins" (o Valorant etc. não vêm instalados: o app baixa quando o usuário quiser), "tls" (só o plugin do
; Valorant usa HTTPS e ele traz o seu próprio tls) e vc_redist.x64.exe (não é usado em tempo de execução: as DLLs do runtime
; do Visual C++ vão à parte, abaixo). dxcompiler.dll e dxil.dll (shaders Direct3D12 do Qt) também: o shear pinta por software
; e abre/exporta normalmente sem eles.
Source: "{#AppDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; \
  Excludes: "CMakeFiles,shear_autogen,*.cmake,CTestTestfile.cmake,ffplay.exe,ffprobe.exe,*.ilk,*.pdb,*.lib,*.exp,plugins,tls,vc_redist.x64.exe,dxcompiler.dll,dxil.dll"
#if VcRedist != ""
; Runtime do Visual C++ junto do app: funciona em PCs que nunca o instalaram.
Source: "{#VcRedist}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\shear.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\shear.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\shear.exe"; Description: "Abrir o shear"; Flags: nowait postinstall skipifsilent

; Item "Cortar com shear" do botão direito (marcado por padrão; também dá para ligar/desligar nas Configurações do app).
; Ao desinstalar, a chave inteira é removida (mesmo se foi criada pelo app).
[Registry]
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Shear"; ValueType: string; ValueName: ""; ValueData: "Cortar com shear"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Shear"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\shear.exe"",0"; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Shear\command"; ValueType: string; ValueName: ""; ValueData: """{app}\shear.exe"" ""%1"""; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Shear"; ValueType: string; ValueName: ""; ValueData: "Cortar com shear"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Shear"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\shear.exe"",0"; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Shear\command"; ValueType: string; ValueName: ""; ValueData: """{app}\shear.exe"" ""%1"""; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Shear"; ValueType: string; ValueName: ""; ValueData: "Cortar com shear"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Shear"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\shear.exe"",0"; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Shear\command"; ValueType: string; ValueName: ""; ValueData: """{app}\shear.exe"" ""%1"""; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Shear"; ValueType: string; ValueName: ""; ValueData: "Cortar com shear"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Shear"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\shear.exe"",0"; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Shear\command"; ValueType: string; ValueName: ""; ValueData: """{app}\shear.exe"" ""%1"""; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Shear"; ValueType: string; ValueName: ""; ValueData: "Cortar com shear"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Shear"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\shear.exe"",0"; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Shear\command"; ValueType: string; ValueName: ""; ValueData: """{app}\shear.exe"" ""%1"""; Tasks: contextmenu
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Shear"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Shear"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Shear"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Shear"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Shear"; Flags: dontcreatekey uninsdeletekey
