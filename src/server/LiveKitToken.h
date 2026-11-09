#pragma once

#include <QString>

namespace server {

// JWT (HS256) для подключения к комнате LiveKit.
// https://docs.livekit.io/home/server/generating-tokens/
QString createLiveKitToken(const QString &apiKey, const QString &apiSecret,
                           const QString &room, qint64 userId,
                           const QString &displayName, int ttlSeconds = 3600);

} // namespace server
