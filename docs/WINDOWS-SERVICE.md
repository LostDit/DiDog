# Служба Windows для сервера

Сервер умеет работать как служба Windows: без окна консоли, с автозапуском и
перезапуском при сбое.

## Установка

Запустите PowerShell **от имени администратора**:

```powershell
cd "C:\Program Files\MessengerApp"
.\MessengerServer.exe --install-service --port 45000 --tls
```

Что произойдёт:

- создаётся служба `MessengerAppServer` с автозапуском;
- если `--data` не указан, данные пишутся в `C:\ProgramData\MessengerApp`;
- сертификат берётся из `tls\server.crt` рядом с исполняемым файлом
  (или укажите `--tls-cert` и `--tls-key`);
- служба сразу запускается.

Чтобы установить без TLS (например, для тестов в доверенной сети), уберите `--tls`:

```powershell
.\MessengerServer.exe --install-service --port 45000 --data "C:\ProgramData\MessengerApp"
```

## Управление

```powershell
Get-Service MessengerAppServer
Start-Service MessengerAppServer
Stop-Service MessengerAppServer
Restart-Service MessengerAppServer

# журнал приложения
Get-Content "C:\ProgramData\MessengerApp\logs\app-$(Get-Date -Format yyyy-MM-dd).log" -Tail 30
```

## Удаление

```powershell
cd "C:\Program Files\MessengerApp"
.\MessengerServer.exe --uninstall-service
```

Служба останавливается и удаляется; файлы данных в `C:\ProgramData\MessengerApp`
не трогаются — удалите каталог вручную, если он больше не нужен.

## Важно

- Для установки и удаления нужны права администратора (иначе код возврата `6`
  и сообщение «Требуются права администратора»).
- Служба работает от `LocalSystem`; каталог `C:\ProgramData\MessengerApp` должен
  быть доступен на запись.
- Порт должен быть разрешён в брандмауэре: установщик создаёт правило для
  `MessengerServer.exe` (профили «частная сеть» и «домен»).
- Если меняете порт или сертификат, удалите и установите службу заново с новыми
  параметрами.
- Установщик может поставить службу автоматически — галочка
  «Установить сервер как службу Windows (автозапуск, TLS)».
