#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace core::proto {

constexpr int kVersion = 1;

// Имена запросов клиента (docs/protocol.md §4).
namespace type {
inline constexpr const char kHello[] = "hello";
inline constexpr const char kAuthRegister[] = "auth.register";
inline constexpr const char kAuthLogin[] = "auth.login";
inline constexpr const char kAuthResume[] = "auth.resume";
inline constexpr const char kAuthLogout[] = "auth.logout";
inline constexpr const char kChatList[] = "chat.list";
inline constexpr const char kChatOpen[] = "chat.open";
inline constexpr const char kChatDirect[] = "chat.direct";
inline constexpr const char kChatRead[] = "chat.read";
inline constexpr const char kChatCreate[] = "chat.create";
inline constexpr const char kChatMembers[] = "chat.members";
inline constexpr const char kChatAdd[] = "chat.add";
inline constexpr const char kChatLeave[] = "chat.leave";
inline constexpr const char kUserSearch[] = "user.search";
inline constexpr const char kBillingStatus[] = "billing.status";
inline constexpr const char kBillingRedeem[] = "billing.redeem";
inline constexpr const char kThemeList[] = "theme.list";
inline constexpr const char kThemeGet[] = "theme.get";
inline constexpr const char kVoiceJoin[] = "voice.join";
inline constexpr const char kVoiceLeave[] = "voice.leave";
inline constexpr const char kVoiceInvite[] = "voice.invite";
inline constexpr const char kVoiceAccept[] = "voice.accept";
inline constexpr const char kVoiceDecline[] = "voice.decline";
inline constexpr const char kVoiceCancel[] = "voice.cancel";
inline constexpr const char kVoiceSpeaking[] = "voice.speaking";
inline constexpr const char kUpdateManifest[] = "update.manifest";
inline constexpr const char kUpdateDownload[] = "update.download";
inline constexpr const char kMsgSend[] = "msg.send";
inline constexpr const char kMsgEdit[] = "msg.edit";
inline constexpr const char kMsgReact[] = "msg.react";
inline constexpr const char kMsgForward[] = "msg.forward";
inline constexpr const char kMsgDelete[] = "msg.delete";
inline constexpr const char kAttachBegin[] = "attach.begin";
inline constexpr const char kAttachFinish[] = "attach.finish";
inline constexpr const char kAttachGet[] = "attach.get";
inline constexpr const char kAttachEnd[] = "attach.end";
inline constexpr const char kAttachAck[] = "attach.ack";
inline constexpr const char kReceiptDelivered[] = "receipt.delivered";
inline constexpr const char kTyping[] = "typing";
inline constexpr const char kPing[] = "ping";
} // namespace type

// Имена событий сервера (docs/protocol.md §5).
namespace event {
inline constexpr const char kMsg[] = "ev.msg";
inline constexpr const char kMsgEdited[] = "ev.msg.edited";
inline constexpr const char kReaction[] = "ev.reaction";
inline constexpr const char kMsgDeleted[] = "ev.msg.deleted";
inline constexpr const char kReceipt[] = "ev.receipt";
inline constexpr const char kRead[] = "ev.read";
inline constexpr const char kChat[] = "ev.chat";
inline constexpr const char kChatMembers[] = "ev.chat.members";
inline constexpr const char kPresence[] = "ev.presence";
inline constexpr const char kTyping[] = "ev.typing";
inline constexpr const char kVoice[] = "ev.voice";
inline constexpr const char kVoiceInvite[] = "ev.voice.invite";
inline constexpr const char kVoiceAccepted[] = "ev.voice.accepted";
inline constexpr const char kVoiceDeclined[] = "ev.voice.declined";
inline constexpr const char kVoiceCancelled[] = "ev.voice.cancelled";
inline constexpr const char kVoiceSpeaking[] = "ev.voice.speaking";
} // namespace event

// Запрос: {"v":1,"id":"…","type":"…","data":{…}}. Пустой id генерируется.
QJsonObject request(const QString &type, const QJsonObject &data,
                    const QString &id = QString());

// Ответ: {"v":1,"id":…,"reply_to":…,"ok":true,"data":{…}}
QJsonObject successReply(const QString &requestId, const QJsonObject &data = QJsonObject());

// Ошибка: {"v":1,…,"ok":false,"error":{"code","message"}}
QJsonObject errorReply(const QString &requestId, ErrorCode code,
                       const QString &message = QString());

// Событие: {"v":1,"type":"ev","event":"ev.msg","data":{…}}
QJsonObject eventObject(const QString &name, const QJsonObject &data);

QByteArray encode(const QJsonObject &object);
bool decode(const QByteArray &payload, QJsonObject *object, QString *error);

// Разбор ответа: код ошибки и текст (для журналирования и UI).
ErrorCode errorCodeOf(const QJsonObject &reply);
QString errorMessageOf(const QJsonObject &reply);

// Голосовой кадр: chat_id | user_id | seq | Opus-данные.
QByteArray encodeVoice(quint32 chatId, quint32 userId, quint32 sequence,
                       const QByteArray &payload);
bool decodeVoice(const QByteArray &payload, quint32 *chatId, quint32 *userId,
                 quint32 *sequence, QByteArray *data);

// UDP-дейтаграммы голоса: "MV02" (4) + token/user_id (4) + seq (4) + AEAD-данные.
inline constexpr quint32 kVoiceMagic = 0x4D563032;

QByteArray encodeVoiceDatagram(quint32 token, quint32 sequence, const QByteArray &payload);
bool decodeVoiceDatagram(const QByteArray &datagram, quint32 *token, quint32 *sequence,
                         QByteArray *payload);

QByteArray encodeVoiceRelay(quint32 userId, quint32 sequence, const QByteArray &payload);
bool decodeVoiceRelay(const QByteArray &datagram, quint32 *userId, quint32 *sequence,
                      QByteArray *payload);

} // namespace core::proto
