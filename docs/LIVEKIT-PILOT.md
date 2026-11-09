# LiveKit-пилот

Цель: заменить самописный UDP-релей на WebRTC-стек LiveKit (адаптивный
джиттер, FEC/NACK, нормальный Opus), сохранив наш клиент и протокол.

## Архитектура

```
Qt-клиент ──UDP PCM(48k mono s16)──► bridge (Node, @livekit/rtc-node) ──WebRTC──► LiveKit SFU
    ▲                                        │
    └────────── UDP PCM ◄────────────────────┘
```

- Клиент по-прежнему управляет звонком через наш сервер (`voice.join`,
  `voice.invite`, `voice.accept`), но медиа идёт через мост.
- Сервер выдаёт `voice.token` (JWT HS256) для комнаты `chat-<id>`.
- Мост подключается к комнате, публикует микрофон и отдаёт входящий звук.

## 1. LiveKit на VPS

В каталоге `bridge/`:

```bash
# ключи: придумайте key и длинный secret
nano livekit.yaml        # keys: didog: "<secret>"
docker compose up -d
```

Порты (открыть в облаке):

| Порт | Протокол | Назначение |
|---|---|---|
| 7880 | TCP | HTTP/WebSocket API |
| 7881 | TCP | WebRTC (TCP fallback) |
| 7882 | UDP | медиа |

TLS: поставьте LiveKit за reverse-proxy (Caddy/nginx) или используйте
`wss://` через прокси; для пилота допустим `ws://IP:7880`.

## 2. Сервер DiDog

```bash
MessengerServer --port 45000 --voice-port 45001 --data /var/lib/messengerapp --tls \
  --http-port 45080 \
  --livekit-url wss://livekit.example.com \
  --livekit-key didog --livekit-secret "<secret>"
```

Без этих флагов `voice.token` вернёт «LiveKit не настроен».

## 3. Мост

На машине клиента (пилот): установить Node.js 18+, затем:

```powershell
cd bridge
npm install
node index.js --url wss://livekit.example.com --token <jwt> --port 46123
```

Клиент запускает мост сам (кнопка звонка при выбранном «LiveKit (пилот)»),
если `bridge/` лежит рядом с `MessengerClient.exe` и `node` в PATH.

## 4. Клиент

Настройки → Голос → **Голосовой сервер** → «LiveKit (пилот)».
Оба участника должны выбрать этот режим. При ошибке (нет Node/моста) звонок
завершается с сообщением, встроенный релей не затрагивается.

## Ограничения пилота

- Требуется Node.js и `bridge/node_modules` на клиенте (позже упакуем в
  установщик или заменим на нативный SDK).
- E2EE LiveKit не включён: транспорт защищён DTLS-SRTP, но сервер LiveKit
  видит медиа (для E2EE нужен `E2EEManager` и обмен ключами).
- Групповые звонки: LiveKit поддерживает, мост публикует/подписывает все
  дорожки; тестировалось только 1:1.
- Нет индикации «кто говорит» из LiveKit (используем локальный анализ).

## Тест-план

1. `curl http://127.0.0.1:7880` — LiveKit отвечает.
2. `node index.js ...` — в логе `BRIDGE: подключены`, `микрофон опубликован`.
3. Звонок в DiDog: в логах клиента `VOICE: LiveKit-мост запущен`.
4. Слышимость в обе стороны, повторный звонок, завершение по кнопке.
5. Сравнить задержку и качество с встроенным релеем.
