#include "core/Protocol.h"

#include "core/Ids.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace core::proto {
namespace {

QJsonObject withEnvelope(const QJsonObject &body)
{
    QJsonObject object = body;
    object.insert(QStringLiteral("v"), kVersion);
    return object;
}

} // namespace

QJsonObject request(const QString &type, const QJsonObject &data, const QString &id)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id.isEmpty() ? core::newUuid() : id);
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("data"), data);
    return withEnvelope(object);
}

QJsonObject successReply(const QString &requestId, const QJsonObject &data)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), core::newUuid());
    object.insert(QStringLiteral("reply_to"), requestId);
    object.insert(QStringLiteral("ok"), true);
    object.insert(QStringLiteral("data"), data);
    return withEnvelope(object);
}

QJsonObject errorReply(const QString &requestId, ErrorCode code, const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), errorCodeToString(code));
    error.insert(QStringLiteral("message"),
                 message.isEmpty() ? errorCodeUserMessage(code) : message);

    QJsonObject object;
    object.insert(QStringLiteral("id"), core::newUuid());
    object.insert(QStringLiteral("reply_to"), requestId);
    object.insert(QStringLiteral("ok"), false);
    object.insert(QStringLiteral("error"), error);
    return withEnvelope(object);
}

QJsonObject eventObject(const QString &name, const QJsonObject &data)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("ev"));
    object.insert(QStringLiteral("event"), name);
    object.insert(QStringLiteral("data"), data);
    return withEnvelope(object);
}

QByteArray encode(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool decode(const QByteArray &payload, QJsonObject *object, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Некорректный JSON: %1").arg(parseError.errorString());
        }
        return false;
    }

    if (object)
        *object = document.object();
    return true;
}

ErrorCode errorCodeOf(const QJsonObject &reply)
{
    if (reply.value(QStringLiteral("ok")).toBool(true))
        return ErrorCode::None;

    const QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    return errorCodeFromString(error.value(QStringLiteral("code")).toString());
}

QString errorMessageOf(const QJsonObject &reply)
{
    if (reply.value(QStringLiteral("ok")).toBool(true))
        return QString();

    const QJsonObject error = reply.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? errorCodeUserMessage(errorCodeOf(reply)) : message;
}

QByteArray encodeVoice(quint32 chatId, quint32 userId, quint32 sequence,
                       const QByteArray &payload)
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << chatId << userId << sequence;
    result.append(payload);
    return result;
}

bool decodeVoice(const QByteArray &payload, quint32 *chatId, quint32 *userId,
                 quint32 *sequence, QByteArray *data)
{
    if (payload.size() < 12)
        return false;

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 chat = 0;
    quint32 user = 0;
    quint32 seq = 0;
    stream >> chat >> user >> seq;

    if (stream.status() != QDataStream::Ok)
        return false;

    if (chatId)
        *chatId = chat;
    if (userId)
        *userId = user;
    if (sequence)
        *sequence = seq;
    if (data)
        *data = payload.mid(12);
    return true;
}

QByteArray encodeVoiceDatagram(quint32 token, quint32 sequence, const QByteArray &payload)
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << kVoiceMagic << token << sequence;
    result.append(payload);
    return result;
}

bool decodeVoiceDatagram(const QByteArray &datagram, quint32 *token, quint32 *sequence,
                         QByteArray *payload)
{
    if (datagram.size() < 12)
        return false;

    QDataStream stream(datagram);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 magic = 0;
    quint32 tokenValue = 0;
    quint32 seq = 0;
    stream >> magic >> tokenValue >> seq;

    if (stream.status() != QDataStream::Ok || magic != kVoiceMagic)
        return false;

    if (token)
        *token = tokenValue;
    if (sequence)
        *sequence = seq;
    if (payload)
        *payload = datagram.mid(12);
    return true;
}

QByteArray encodeVoiceRelay(quint32 userId, quint32 sequence, const QByteArray &payload)
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << kVoiceMagic << userId << sequence;
    result.append(payload);
    return result;
}

bool decodeVoiceRelay(const QByteArray &datagram, quint32 *userId, quint32 *sequence,
                      QByteArray *payload)
{
    if (datagram.size() < 12)
        return false;

    QDataStream stream(datagram);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 magic = 0;
    quint32 user = 0;
    quint32 seq = 0;
    stream >> magic >> user >> seq;

    if (stream.status() != QDataStream::Ok || magic != kVoiceMagic)
        return false;

    if (userId)
        *userId = user;
    if (sequence)
        *sequence = seq;
    if (payload)
        *payload = datagram.mid(12);
    return true;
}

} // namespace core::proto
