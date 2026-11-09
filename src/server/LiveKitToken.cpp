#include "server/LiveKitToken.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>

namespace server {
namespace {

QByteArray base64Url(const QByteArray &data)
{
    QByteArray encoded = data.toBase64(QByteArray::Base64UrlEncoding
                                       | QByteArray::OmitTrailingEquals);
    return encoded;
}

} // namespace

QString createLiveKitToken(const QString &apiKey, const QString &apiSecret,
                           const QString &room, qint64 userId,
                           const QString &displayName, int ttlSeconds)
{
    if (apiKey.isEmpty() || apiSecret.isEmpty() || room.isEmpty() || userId <= 0)
        return QString();

    const qint64 now = QDateTime::currentSecsSinceEpoch();

    QJsonObject header;
    header.insert(QStringLiteral("alg"), QStringLiteral("HS256"));
    header.insert(QStringLiteral("typ"), QStringLiteral("JWT"));

    QJsonObject video;
    video.insert(QStringLiteral("roomJoin"), true);
    video.insert(QStringLiteral("room"), room);
    video.insert(QStringLiteral("canPublish"), true);
    video.insert(QStringLiteral("canSubscribe"), true);

    QJsonObject payload;
    payload.insert(QStringLiteral("iss"), apiKey);
    payload.insert(QStringLiteral("sub"), QString::number(userId));
    payload.insert(QStringLiteral("name"), displayName);
    payload.insert(QStringLiteral("nbf"), static_cast<double>(now - 10));
    payload.insert(QStringLiteral("exp"), static_cast<double>(now + ttlSeconds));
    payload.insert(QStringLiteral("video"), video);

    const QByteArray headerPart = base64Url(
        QJsonDocument(header).toJson(QJsonDocument::Compact));
    const QByteArray payloadPart = base64Url(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const QByteArray signingInput = headerPart + '.' + payloadPart;

    const QByteArray signature = QMessageAuthenticationCode::hash(
        signingInput, apiSecret.toUtf8(), QCryptographicHash::Sha256);

    return QString::fromLatin1(signingInput + '.' + base64Url(signature));
}

} // namespace server
