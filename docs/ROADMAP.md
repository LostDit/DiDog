# План работ

Статусы: `[ ]` — не начато, `[~]` — в работе, `[x]` — готово и проверено сборкой/тестами.

## M0. Каркас и сборка

- [x] `git init`, `.gitignore`, `.gitattributes`, LICENSE
- [x] CMake (Qt 5.14+, C++17, все модули), `CMakePresets.json`
- [x] Логирование в файл (`qInstallMessageHandler`, ротация)
- [x] `AppPaths`: каталоги данных, `MESSENGER_DATA_DIR`, `--profile`
- [x] `ThemeManager` + `base.qss` с токенами, светлая и тёмная темы
- [x] `SettingsStore` (INI в каталоге данных)
- [x] Single-instance (`QLockFile`)
- [x] Каркас окна клиента, `AvatarWidget`, иконки
- [x] Консольный сервер: аргументы, лог, баннер

Критерий: собирается MinGW, запускается окно, тема переключается на лету.

## M1. Данные и безопасность

- [x] `Database` (WAL, busy_timeout, foreign_keys), `Migrations` по `user_version`
- [x] Карантин повреждённой БД и пересоздание (`*.corrupt-<ts>`)
- [x] `PasswordHasher` (PBKDF2-HMAC-SHA256), валидация логина/пароля/имени/файлов
- [x] `tst_password`, `tst_validation`, `tst_db` (схема, версия, переоткрытие, повреждение, FK)
- [x] Репозитории: пользователи, чаты, участники, сообщения, вложения, квитанции, outbox
- [x] `tst_repositories`: идемпотентность, пагинация, статусы, непрочитанные, вложения, очередь

Критерий: БД создаётся и мигрирует, тесты зелёные.

## M2. Протокол и сервер

- [x] `FrameCodec`, `Protocol` (JSON-конверт, коды ошибок), `JsonMapping`
- [x] `ServerCore`: сессии, auth (register/login/resume/logout), chat.list/open/direct/read, msg.send, receipts, typing, presence
- [x] `ClientSession`: разбор кадров, диспетчеризация, ответы и события
- [x] `tst_protocol`, `tst_server_smoke` (два клиента, ack, идемпотентность, ev.msg/ev.read/ev.receipt)
- [ ] `AttachmentStore`: приём чанками, sha256, контент-адресация

Критерий: headless smoke — два клиента обменялись текстом, ack и read корректы.

## M3. Клиентская сеть

- [ ] `ClientConnection`, `RequestTracker` (id → callback, таймауты)
- [ ] `ReconnectPolicy`, автопереподключение
- [ ] `SessionService`: login/register/resume, токен в настройках
- [ ] `OutboxService`: офлайн-очередь, повторная отправка без дублей

Критерий: падение сервера не теряет сообщения, после подъёма всё доезжает ровно один раз.

## M4. UI ядра

- [ ] `LoginWindow`, `RegisterDialog`
- [ ] `MainWindow`: список чатов, шапка, лента, композер
- [ ] `MessageListModel`, `ChatListModel`, `MessageBubble`, группировка, разделители дат
- [ ] Пагинация, автоскролл, статусы, счётчики непрочитанных
- [ ] Темы на лету, анимация появления, пустые состояния

Критерий: два окна переписываются в реальном времени, история после рестарта.

## M5. Медиа

- [ ] Отправка файлов чанками, прогресс, лимит 25 МБ
- [ ] Миниатюры (пул потоков), лайтбокс фото, GIF через `QMovie`, видеоплеер
- [ ] Плитка файла, «сохранить как», потерянный файл

Критерий: фото/GIF/видео/файл ходят между клиентами.

## M6. Полировка

- [ ] Присутствие, «печатает…», трей и уведомления, поиск
- [ ] RU/EN (`QTranslator`), настройки
- [ ] Скелетоны загрузки, баннеры ошибок, тосты

## M7. Поставка

- [ ] README со скриншотами, `windeployqt`-пакет
- [ ] GitHub Actions: Windows (MSVC) + Ubuntu 22.04
- [ ] Финальный прогон чек-листа DoD (TZ-v2 §11)
