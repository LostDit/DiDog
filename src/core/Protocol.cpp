#include "core/Protocol.h"

#include "core/Ids.h"

#include <QCoreApplication>
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

} // namespace core::proto
