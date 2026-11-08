# Протокол MessengerApp v1

Транспорт: TCP, по умолчанию `127.0.0.1:45000`. Кодировка — UTF-8. Числа в JSON — целые (`int64`), время — unix-миллисекунды UTC.

## 1. Кадрирование

Каждый кадр = заголовок 8 байт + payload:

```
+--------+--------+--------+--------+--------+--------+--------+--------+
|         payload_len (uint32, big-endian)         | type   | flags  | rsv (uint16 BE) |
+--------+--------+--------+--------+--------+--------+--------+--------+
|                             payload                                |
+--------------------------------------------------------------------+
```

| Поле | Размер | Описание |
|---|---|---|
| `payload_len` | 4 | длина payload, **не включая** заголовок; 0 — допустим |
| `type` | 1 | `1` — JSON, `2` — бинарный чанк файла |
| `flags` | 1 | зарезервировано, `0`; получатель игнорирует |
| `rsv` | 2 | зарезервировано, `0` |

Правила:
- `payload_len > 1 048 576` (1 МиБ) → кадр отбрасывается, соединение закрывается с `BAD_FRAME`;
- для `type=2` payload ≥ 8 байт и ≤ 262 144 байт;
- частичные кадры буферизуются; несколько кадров в одном TCP-сегменте — штатный случай;
- неизвестный `type` → `BAD_FRAME` + закрытие.

### 1.1. Бинарный чанк (`type=2`)

```
uint32 transfer_id | uint32 seq | bytes payload
```

`seq` начинается с 0 и растёт на 1; разрыв → `BAD_FRAME`. Сервер принимает чанки только для активной передачи, начатой этим соединением.

## 2. JSON-конверт

### 2.1. Запрос (клиент → сервер)

```json
{ "v": 1, "id": "9f1c…", "type": "msg.send", "data": { } }
```

- `v` — версия протокола (сейчас `1`); несовпадение → `VERSION_UNSUPPORTED`;
- `id` — UUID v4 запроса, уникален в пределах соединения (для сопоставления ответа и логов);
- `data` — объект, схема зависит от `type`.

### 2.2. Ответ (сервер → клиент)

Успех:
```json
{ "v": 1, "id": "…", "reply_to": "9f1c…", "ok": true, "data": { } }
```
Ошибка:
```json
{ "v": 1, "id": "…", "reply_to": "9f1c…", "ok": false,
  "error": { "code": "AUTH_FAILED", "message": "Неверный логин или пароль" } }
```
`message` — текст для пользователя (RU), `code` — машинный.

### 2.3. Событие (сервер → клиент, без запроса)

```json
{ "v": 1, "type": "ev", "event": "ev.msg", "data": { } }
```

## 3. Последовательность установления сессии

```
client → server : hello {app, proto}
server → client : hello.ok {server_time, proto, min_client}
client → server : auth.login | auth.register | auth.resume
server → client : auth.ok {token, user} | error
```

После `auth.ok` соединение считается авторизованным; любые запросы кроме `hello`, `auth.*`, `ping` до этого → `AUTH_REQUIRED`.

## 4. Запросы клиента

### `hello`
| Поле | Тип | Обяз. | Описание |
|---|---|---|---|
| `app` | string | да | `MessengerClient/1.0.0` |
| `proto` | int | да | `1` |

Ответ: `{server_time, proto, min_client}`.

### `auth.register`
`{username, display_name, password}` → `{token, user}`; ошибки `USER_EXISTS`, `BAD_REQUEST`.
При регистрации пользователь автоматически добавляется в «Общий чат».

### `auth.login`
`{username, password}` → `{token, user}`; ошибка `AUTH_FAILED` (без уточнения причины).

### `auth.resume`
`{token}` → `{token, user}`; ошибка `AUTH_FAILED` (токен истёк/неизвестен).

### `auth.logout`
`{}` → `{}`; токен удаляется на сервере.

### `chat.list`
`{}` → `{chats: [chat]}`.

`chat`:
```json
{ "id": 1, "kind": "group", "title": "Общий чат",
  "peer": {"id":2,"display_name":"Аня","avatar_hue":120},   // null для group
  "last_message": {"seq":41,"kind":"text","body":"Привет","sender_id":2,
                   "created_at":1699360000000},
  "unread_count": 3, "last_seq": 41 }
```

### `chat.open`
`{chat_id, before_seq?, limit?}` (limit по умолчанию 50, максимум 100) → `{messages: [message], has_more: bool}`.
Сообщения в порядке возрастания `seq`. `before_seq` не задан — последние `limit`.

### `chat.direct`
`{username}` → `{chat: chat}` — ищет или создаёт личный чат с пользователем.

### `chat.read`
`{chat_id, up_to_seq}` → `{}`; обновляет `last_read_seq`, создаёт `read`-квитанции, рассылает `ev.read` всем участникам, включая отправителя.

### `msg.send`
```json
{ "chat_id": 3, "client_msg_id": "c0a8…", "kind": "text",
  "body": "Привет", "attachment_id": null }
```
`kind ∈ {text,image,video,gif,file}`; для `text` — `body` непустой, `attachment_id` null; для остальных — `attachment_id` обязателен, `body` — опциональная подпись (≤ 4000).

Ответ (ack): `{message_id, seq, created_at}`.
Повтор с тем же `client_msg_id` → тот же ответ (идемпотентность), событие `ev.msg` повторно **не** рассылается.

### `msg.edit`
`{chat_id, message_id, body}` → `{message}`; редактировать можно только свои текстовые
сообщения, `body` проверяется как при отправке. Сервер рассылает `ev.msg.edited`
остальным участникам. В ответе и событии — полное сообщение с `edited_at`.

### `msg.delete`
`{chat_id, message_id}` → `{message}`; удалять у всех можно только свои сообщения.
Сервер ставит `deleted = 1`, очищает `body` и рассылает `ev.msg.deleted`
(вместо сообщения остаётся tombstone). История и `chat.open` возвращают
сообщение с `deleted: true`.

### `chat.create`
```json
{ "title": "Друзья", "member_ids": [2, 5] }
```
→ `{chat}` (summary для создателя, `kind: "group"`). Создатель получает роль `owner`,
приглашённые — `member` и событие `ev.chat`. Название 2–64 символа, до 50 участников.

### `chat.members`
`{chat_id}` → `{members: [{id, username, display_name, avatar_hue, role, online}]}`;
роли: `owner`, `admin`, `member`; `online` — есть активная сессия.

### `chat.add`
`{chat_id, user_ids: [int]}` → `{}`; только `owner`/`admin`. Добавленные получают
`ev.chat`, остальные участники — `ev.chat.members`.

### `chat.leave`
`{chat_id}` → `{}`; владелец передаёт роль следующему участнику. Если участников
не осталось, чат удаляется.

### `user.search`
`{query, limit?}` → `{users: […]}`; поиск по логину и имени, без текущего пользователя.

### `attach.begin`
`{chat_id, file_name, mime, byte_size, sha256}` →
`{transfer_id, chunk_size}`; ошибки `TOO_LARGE`, `RATE_LIMITED`.
Бронирует передачу; слот живёт 10 минут, иначе освобождается.

### `attach.finish`
`{transfer_id}` → `{attachment_id, byte_size, sha256}`; ошибки `NOT_FOUND` (нет передачи), `BAD_REQUEST` (размер/sha256 не совпали).

### `attach.get`
`{attachment_id}` → `{transfer_id, byte_size, sha256, chunk_size}`; далее сервер шлёт `type=2` чанки, завершает `attach.end {transfer_id}`.

### `receipt.delivered`
`{message_ids: [int]}` → `{}`; клиент подтверждает получение, сервер пишет `delivered` и пересылает отправителю `ev.receipt`.

### `typing`
`{chat_id, on: bool}` → `{}`; сервер рассылает `ev.typing` остальным участникам (троттлинг 2 с на стороне клиента).

### `ping`
`{}` → `{server_time}`.

### `search.messages` (P1)
`{query, chat_id?, limit?}` → `{results: [{message_id, chat_id, seq, snippet}]}`.

## 5. События сервера

| event | data | когда |
|---|---|---|
| `ev.msg` | `{message}` | новое сообщение в чате пользователя |
| `ev.msg.edited` | `{chat_id, message}` | сообщение изменено |
| `ev.msg.deleted` | `{chat_id, message}` | сообщение удалено у всех (`deleted: true`) |
| `ev.receipt` | `{chat_id, message_id, user_id, state}` | квитанция delivered/read |
| `ev.read` | `{chat_id, user_id, up_to_seq}` | пользователь прочитал чат |
| `ev.chat` | `{chat}` | создан личный или групповой чат (второму участнику) |
| `ev.chat.members` | `{chat_id}` | состав группы изменился |
| `ev.presence` | `{user_id, online, last_seen}` | подключение/отключение |
| `ev.typing` | `{chat_id, user_id, on}` | набор текста |

`message`:
```json
{ "id": 128, "chat_id": 3, "seq": 42, "sender": {"id":2,"username":"anya","display_name":"Аня","avatar_hue":120},
  "kind": "image", "body": "", "created_at": 1699360000000,
  "deleted": false, "edited_at": 0,
  "attachment": {"id":7,"file_name":"photo.jpg","mime":"image/jpeg","byte_size":182734,
                 "width":1280,"height":720,"duration_ms":null,"sha256":"…"},
  "status": "sent" }
```

## 6. Передача вложения (upload)

```
C: attach.begin {chat_id, file_name, mime, byte_size, sha256}
S: {transfer_id: 11, chunk_size: 65536}
C: type=2 [11, 0, data…]        // 64 КиБ
C: type=2 [11, 1, data…]
…
C: attach.finish {transfer_id: 11}
S: {attachment_id: 7, byte_size, sha256}
C: msg.send {chat_id, client_msg_id, kind:"image", attachment_id: 7}
S: {message_id, seq, created_at}
S → все: ev.msg
```

Сервер:
1. пишет в `tmp/<uuid>.part`;
2. по `finish` проверяет `byte_size` и `sha256`, переименовывает в `attachments/<sha[0:2]>/<sha>`;
3. при ошибке удаляет temp и отвечает `BAD_REQUEST`;
4. при разрыве соединения — temp удаляется.

## 7. Скачивание

```
C: attach.get {attachment_id: 7}
S: {transfer_id: 12, byte_size, sha256, chunk_size}
S: type=2 [12, 0, data…] …
S: attach.end {transfer_id: 12}
C: attach.ack {transfer_id: 12}      // только после проверки sha256
```
Клиент пишет в `tmp/`, проверяет хэш, атомарно переименовывает, обновляет запись `attachments`.

## 8. Таймауты и переподключение

| Параметр | Значение |
|---|---|
| `ping` | каждые 30 с |
| idle-таймаут сервера | 120 с без кадров → закрытие |
| таймаут запроса | 30 с → `TIMEOUT` на клиенте (запрос остаётся в outbox) |
| реконнект | 1, 2, 4, 8, 16, 30 с (максимум), +jitter 0–20 % |
| срок жизни transfer | 10 мин |
| TTL токена | 30 дней |

## 9. Коды ошибок

| Код | HTTP-аналог | Комментарий |
|---|---|---|
| `BAD_FRAME` | 400 | нарушен фрейминг/лимит |
| `BAD_REQUEST` | 400 | невалидный JSON/поля |
| `VERSION_UNSUPPORTED` | 400 | `v` ≠ 1 |
| `AUTH_REQUIRED` | 401 | запрос до авторизации |
| `AUTH_FAILED` | 401 | неверные данные/токен |
| `USER_EXISTS` | 409 | логин занят |
| `NOT_MEMBER` | 403 | нет доступа к чату |
| `NOT_FOUND` | 404 | чат/сообщение/передача |
| `TOO_LARGE` | 413 | превышен лимит |
| `RATE_LIMITED` | 429 | превышен лимит сообщений |
| `CONFLICT` | 409 | `seq`/уникальность |
| `INTERNAL` | 500 | ошибка сервера |

## 10. Пример обмена

```json
{"v":1,"id":"1","type":"hello","data":{"app":"MessengerClient/1.0.0","proto":1}}
{"v":1,"id":"2","reply_to":"1","ok":true,"data":{"server_time":1699360000000,"proto":1,"min_client":"1.0.0"}}
{"v":1,"id":"3","type":"auth.login","data":{"username":"anya","password":"secret123"}}
{"v":1,"id":"4","reply_to":"3","ok":true,"data":{"token":"a3f1…","user":{"id":2,"username":"anya","display_name":"Аня","avatar_hue":120}}}
{"v":1,"id":"5","type":"chat.open","data":{"chat_id":1,"limit":50}}
{"v":1,"id":"6","reply_to":"5","ok":true,"data":{"messages":[],"has_more":false}}
{"v":1,"type":"ev","event":"ev.msg","data":{"message":{"id":1,"chat_id":1,"seq":1,"sender":{"id":2,"username":"anya","display_name":"Аня","avatar_hue":120},"kind":"text","body":"Привет!","created_at":1699360001000,"attachment":null,"status":"sent"}}}
```
