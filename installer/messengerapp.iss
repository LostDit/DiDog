; Установщик MessengerApp (Inno Setup 6.3+).
; Сборка: ISCC.exe installer\messengerapp.iss
; Требует предварительно собранный dist\MessengerApp (scripts\package-windows.ps1).

#define AppName "MessengerApp"
#define AppVersion "2.0.0"
#define AppPublisher "LostDit"
#define AppUrl "https://github.com/LostDit/MessengerApp"
#define ClientExe "MessengerClient.exe"
#define ServerExe "MessengerServer.exe"
#define SourceDir "..\dist\MessengerApp"

[Setup]
AppId={{9D2F4A61-3C7B-4E52-9B0D-6A1E8C4F2B77}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\dist\installer
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#ClientExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "firewall"; Description: "Разрешить сервер в брандмауэре Windows (для работы в локальной сети)"; GroupDescription: "Сеть:"
Name: "service"; Description: "Установить сервер как службу Windows (автозапуск, TLS)"; GroupDescription: "Сервер:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#ClientExe}"; WorkingDir: "{app}"
Name: "{group}\Сервер {#AppName}"; Filename: "{app}\{#ServerExe}"; WorkingDir: "{app}"; Comment: "Локальный сервер: держите окно открытым, пока работают клиенты"
Name: "{group}\Удалить {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ClientExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MessengerApp Server"""; Flags: runhidden waituntilterminated; Tasks: firewall
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""MessengerApp Server"" dir=in action=allow program=""{app}\{#ServerExe}"" enable=yes profile=private,domain"; Flags: runhidden waituntilterminated; Tasks: firewall
Filename: "{app}\{#ServerExe}"; Parameters: "--install-service --port 45000 --data ""{commonappdata}\MessengerApp"" --tls"; Flags: runhidden waituntilterminated; Tasks: service; StatusMsg: "Установка службы сервера…"
Filename: "{app}\{#ServerExe}"; Description: "Запустить сервер {#AppName}"; Flags: nowait postinstall skipifsilent unchecked
Filename: "{app}\{#ClientExe}"; Description: "Запустить {#AppName}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MessengerApp Server"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveFirewallRule"
Filename: "{app}\{#ServerExe}"; Parameters: "--uninstall-service"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveService"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\attachments"
Type: filesandordirs; Name: "{app}\thumbnails"
