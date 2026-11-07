# Развёртывание сервера на VPS

Сценарий: сервер постоянно работает на VPS с публичным адресом, клиенты подключаются
из любых сетей по TLS.

## 1. Что понадобится

- VPS с Ubuntu 22.04+ (1 ГБ RAM достаточно), публичный IPv4.
- Домен или просто IP-адрес.
- Открытый порт `45000/tcp` в панели провайдера (облачный firewall).

## 2. Установка одной командой

Скопируйте репозиторий на сервер и запустите:

```bash
git clone https://github.com/LostDit/MessengerApp.git
cd MessengerApp
sudo ./scripts/deploy-vps.sh
```

Скрипт:

1. ставит зависимости Qt и OpenSSL;
2. собирает `MessengerServer` в Release;
3. кладёт его в `/opt/messengerapp`, данные — в `/var/lib/messengerapp`;
4. создаёт отдельного системного пользователя `messengerapp`;
5. генерирует самоподписанный сертификат в `/opt/messengerapp/tls`, если его нет;
6. создаёт и запускает systemd-службу `messengerapp` с TLS;
7. открывает порт в `ufw`, если он включён;
8. печатает SHA-256 отпечаток сертификата.

## 3. Проверка

```bash
systemctl status messengerapp
journalctl -u messengerapp -n 50 --no-pager
ss -ltnp | grep 45000
```

В логах будет строка с отпечатком сертификата — он понадобится для сверки в клиенте.

## 4. Подключение клиентов

1. Запустите MessengerApp, в поле сервера введите IP или домен VPS, порт `45000`.
2. Поставьте галочку **«Защищённое соединение (TLS)»**.
3. При первом подключении клиент запомнит отпечаток сертификата.
   Сравните его с отпечатком из логов сервера (первые 16 символов выводятся в логе клиента).
4. Если сертификат на сервере перевыпущен, удалите сохранённый отпечаток:
   остановите клиент и удалите ключ `tls/pin/<host>_<port>` в `settings.ini`
   (или удалите `settings.ini`, чтобы начать заново).

## 5. Служба: управление

```bash
sudo systemctl restart messengerapp
sudo systemctl stop messengerapp
sudo systemctl disable --now messengerapp
journalctl -u messengerapp -f        # живой лог
```

Обновление версии:

```bash
cd MessengerApp && git pull
cmake --build build -j"$(nproc)"
sudo systemctl stop messengerapp
sudo install build/MessengerServer /opt/messengerapp/MessengerServer
sudo systemctl start messengerapp
```

## 6. Сертификат

Самоподписанный сертификат подходит, потому что клиент проверяет его по отпечатку
(TOFU), а не по цепочке доверия. Перевыпуск:

```bash
sudo ./scripts/make-cert.sh /opt/messengerapp/tls
sudo systemctl restart messengerapp
```

После перевыпуска все клиенты один раз попросят принять новый отпечаток.

Для домена можно выпустить Let's Encrypt-сертификат (`certbot certonly --standalone -d chat.example.org`)
и указать пути через `--tls-cert`/`--tls-key` в `ExecStart`.

## 7. Резервные копии

Всё состояние — в `/var/lib/messengerapp`:

- `server.db` (+ `-wal`, `-shm`) — пользователи, чаты, сообщения;
- `attachments/` — файлы;
- `logs/` — журналы.

```bash
sudo systemctl stop messengerapp
sudo tar czf messengerapp-backup-$(date +%F).tar.gz -C /var/lib messengerapp
sudo systemctl start messengerapp
```

## 8. Безопасность

- TLS шифрует канал, но сертификат самоподписанный: подлинность проверяется по
  отпечатку, поэтому первый контакт должен проходить по доверенному каналу
  (тот же отпечаток виден в логах сервера).
- Пароли хранятся как PBKDF2-HMAC-SHA256 (120 000 итераций).
- Клиентские сертификаты не используются: доступ даёт только логин и пароль.
- Ограничьте доступ по IP в облачном firewall, если аудитория известна.
- Не запускайте сервер от `root`: скрипт создаёт отдельного пользователя.

## 9. Если TLS не работает

| Симптом | Причина |
|---|---|
| «Не удалось подключиться к диспетчеру» в логах | сервер запущен без `--tls`, а клиент с галочкой (или наоборот) |
| `QSslSocket::supportsSsl() == false` на Ubuntu | не установлены OpenSSL/плагины Qt, проверьте `ldd MessengerServer | grep ssl` |
| «Сертификат сервера не совпадает» | сертификат перевыпущен — удалите сохранённый отпечаток в настройках клиента |
| Соединение сбрасывается | порт закрыт в облачном firewall или `ufw` |
