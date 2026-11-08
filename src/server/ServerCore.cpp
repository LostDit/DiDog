#include "server/ServerCore.h"

#include "core/Ids.h"
#include "core/Limits.h"
#include "core/Protocol.h"
#include "data/AttachmentRepository.h"
#include "data/ChatRepository.h"
#include "data/JsonMapping.h"
#include "data/MessageRepository.h"
#include "data/ReceiptRepository.h"
#include "data/UserRepository.h"
#include "security/PasswordHasher.h"
#include "security/Validator.h"
#include "server/ClientSession.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace server {
namespace {

constexpr int kTokenTtlDays = 30;

qint64 int64Value(const QJsonObject &object, const char *key)
{
    return static_cast<qint64>(object.value(QLatin1String(key)).toDouble());
}

QString stringValue(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

QJsonObject serverInfo()
{
    QJsonObject info;
    info.insert(QStringLiteral("server_time"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    info.insert(QStringLiteral("proto"), core::proto::kVersion);
    info.insert(QStringLiteral("min_client"), QStringLiteral("1.0.0"));
    return info;
}

QJsonObject messageAck(const data::Message &message)
{
    QJsonObject ack;
    ack.insert(QStringLiteral("message_id"), static_cast<double>(message.id));
    ack.insert(QStringLiteral("seq"), static_cast<double>(message.seq));
    ack.insert(QStringLiteral("created_at"), static_cast<double>(message.createdAt));
    return ack;
}

} // namespace

ServerCore::ServerCore(QObject *parent)
    : QObject(parent)
    , m_database(QStringLiteral("server_db"))
    , m_attachmentStore(QString())
{
}

ServerCore::~ServerCore() = default;

bool ServerCore::initialize(const QString &databasePath, const QString &attachmentsRoot,
                            QString *error)
{
    m_attachmentsRoot = attachmentsRoot;
    m_attachmentStore.setRoot(attachmentsRoot);

    if (!m_database.open(databasePath, error))
        return false;

    data::ChatRepository chats(m_database.handle());
    data::Chat general;
    if (!chats.ensureGeneralChat(&general, error))
        return false;

    qInfo().noquote() << QStringLiteral("База сервера: %1 (общий чат #%2)")
                             .arg(databasePath)
                             .arg(general.id);
    return true;
}

HandlerResult ServerCore::handle(const QString &type, const QJsonObject &data,
                                 ClientSession *session)
{
    const bool isPublic = (type == QLatin1String(core::proto::type::kHello)                           || type == QLatin1String(core::proto::type::kAuthRegister)
                           || type == QLatin1String(core::proto::type::kAuthLogin)
                           || type == QLatin1String(core::proto::type::kAuthResume)
                           || type == QLatin1String(core::proto::type::kPing));

    if (!isPublic && (!session || !session->isAuthenticated()))
        return HandlerResult::failure(core::ErrorCode::AuthRequired);

    if (type == QLatin1String(core::proto::type::kHello))
        return handleHello(data);
    if (type == QLatin1String(core::proto::type::kAuthRegister))
        return handleRegister(data, session);
    if (type == QLatin1String(core::proto::type::kAuthLogin))
        return handleLogin(data, session);
    if (type == QLatin1String(core::proto::type::kAuthResume))
        return handleResume(data, session);
    if (type == QLatin1String(core::proto::type::kAuthLogout))
        return handleLogout(session);

    if (type == QLatin1String(core::proto::type::kChatList))
        return handleChatList(session);
    if (type == QLatin1String(core::proto::type::kChatOpen))
        return handleChatOpen(data, session);
    if (type == QLatin1String(core::proto::type::kChatDirect))
        return handleChatDirect(data, session);
    if (type == QLatin1String(core::proto::type::kChatRead))
        return handleChatRead(data, session);

    if (type == QLatin1String(core::proto::type::kMsgSend))
        return handleMessageSend(data, session);
    if (type == QLatin1String(core::proto::type::kMsgEdit))
        return handleMessageEdit(data, session);
    if (type == QLatin1String(core::proto::type::kMsgDelete))
        return handleMessageDelete(data, session);
    if (type == QLatin1String(core::proto::type::kReceiptDelivered))
        return handleReceiptDelivered(data, session);
    if (type == QLatin1String(core::proto::type::kTyping))
        return handleTyping(data, session);

    if (type == QLatin1String(core::proto::type::kAttachBegin))
        return handleAttachBegin(data, session);
    if (type == QLatin1String(core::proto::type::kAttachFinish))
        return handleAttachFinish(data, session);
    if (type == QLatin1String(core::proto::type::kAttachGet))
        return handleAttachGet(data, session);

    if (type == QLatin1String(core::proto::type::kPing)) {
        QJsonObject payload;
        payload.insert(QStringLiteral("server_time"),
                       static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
        return HandlerResult::success(payload);
    }

    return HandlerResult::failure(core::ErrorCode::BadRequest,
                                  QStringLiteral("Неизвестный запрос: %1").arg(type));
}

HandlerResult ServerCore::handleHello(const QJsonObject &data)
{
    const int version = data.value(QStringLiteral("proto")).toInt(0);
    if (version != core::proto::kVersion) {
        return HandlerResult::failure(core::ErrorCode::VersionUnsupported,
                                      QStringLiteral("Поддерживается протокол версии %1")
                                          .arg(core::proto::kVersion));
    }
    return HandlerResult::success(serverInfo());
}

HandlerResult ServerCore::handleRegister(const QJsonObject &data, ClientSession *session)
{
    const QString username = stringValue(data, "username").trimmed();
    const QString displayName = stringValue(data, "display_name");
    const QString password = stringValue(data, "password");

    const security::CheckResult usernameCheck = security::checkUsername(username);
    if (!usernameCheck.ok)
        return HandlerResult::failure(core::ErrorCode::BadRequest, usernameCheck.message);

    const security::CheckResult displayCheck = security::checkDisplayName(displayName);
    if (!displayCheck.ok)
        return HandlerResult::failure(core::ErrorCode::BadRequest, displayCheck.message);

    const security::CheckResult passwordCheck = security::checkPassword(password);
    if (!passwordCheck.ok)
        return HandlerResult::failure(core::ErrorCode::BadRequest, passwordCheck.message);

    data::UserRepository users(m_database.handle());
    QString error;
    bool exists = false;
    if (!users.usernameExists(username, &exists, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (exists)
        return HandlerResult::failure(core::ErrorCode::UserExists);

    const security::PasswordHash hash = security::PasswordHasher::create(password);
    const int hue = QRandomGenerator::global()->bounded(360);

    data::User user;
    if (!users.insert(username, security::normalizeDisplayName(displayName), hash, hue, &user,
                      &error)) {
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    }

    data::ChatRepository chats(m_database.handle());
    data::Chat generalChat;
    if (!chats.ensureGeneralChat(&generalChat, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!chats.addMember(generalChat.id, user.id, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    const QString token = issueToken(user.id, &error);
    if (token.isEmpty())
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    session->setAuthenticated(user.id, user.username);
    session->setToken(token);
    registerSession(session);
    broadcastPresence(user.id, true);

    qInfo().noquote() << QStringLiteral("Регистрация: %1 (#%2)").arg(user.username).arg(user.id);

    QJsonObject payload;
    payload.insert(QStringLiteral("token"), token);
    payload.insert(QStringLiteral("user"), data::json::userToJson(user));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleLogin(const QJsonObject &data, ClientSession *session)
{
    const QString username = stringValue(data, "username").trimmed();
    const QString password = stringValue(data, "password");

    data::UserRepository users(m_database.handle());
    QString error;
    data::User user;
    security::PasswordHash hash;

    if (!users.findAuth(username, &user, &hash, &error))
        return HandlerResult::failure(core::ErrorCode::AuthFailed);

    if (!security::PasswordHasher::verify(password, hash))
        return HandlerResult::failure(core::ErrorCode::AuthFailed);

    const QString token = issueToken(user.id, &error);
    if (token.isEmpty())
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    session->setAuthenticated(user.id, user.username);
    session->setToken(token);
    registerSession(session);
    broadcastPresence(user.id, true);

    qInfo().noquote() << QStringLiteral("Вход: %1 (#%2)").arg(user.username).arg(user.id);

    QJsonObject payload;
    payload.insert(QStringLiteral("token"), token);
    payload.insert(QStringLiteral("user"), data::json::userToJson(user));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleResume(const QJsonObject &data, ClientSession *session)
{
    const QString token = stringValue(data, "token");
    if (token.isEmpty())
        return HandlerResult::failure(core::ErrorCode::AuthFailed);

    qint64 userId = 0;
    QString error;
    if (!resolveToken(token, &userId, &error))
        return HandlerResult::failure(core::ErrorCode::AuthFailed);

    data::UserRepository users(m_database.handle());
    data::User user;
    if (!users.findById(userId, &user, &error))
        return HandlerResult::failure(core::ErrorCode::AuthFailed);

    session->setAuthenticated(user.id, user.username);
    session->setToken(token);
    registerSession(session);
    broadcastPresence(user.id, true);

    QJsonObject payload;
    payload.insert(QStringLiteral("token"), token);
    payload.insert(QStringLiteral("user"), data::json::userToJson(user));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleLogout(ClientSession *session)
{
    if (session) {
        dropToken(session->token());
        const qint64 userId = session->userId();
        unregisterSession(session);
        session->setAuthenticated(0, QString());
        broadcastPresence(userId, false);
    }
    return HandlerResult::success();
}

HandlerResult ServerCore::handleChatList(ClientSession *session)
{
    data::ChatRepository chats(m_database.handle());
    QString error;
    QList<data::ChatSummary> summaries;

    if (!chats.summaries(session->userId(), &summaries, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonArray array;
    for (const data::ChatSummary &summary : summaries)
        array.append(data::json::chatSummaryToJson(summary));

    QJsonObject payload;
    payload.insert(QStringLiteral("chats"), array);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleChatOpen(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const qint64 beforeSeq = int64Value(data, "before_seq");
    const int limit = data.value(QStringLiteral("limit")).toInt(core::limits::kHistoryPageSize);

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    data::MessageRepository messages(m_database.handle());
    QList<data::Message> page;
    bool hasMore = false;
    if (!messages.page(chatId, beforeSeq, limit, &page, &hasMore, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonArray array;
    for (const data::Message &message : page) {
        data::Message withState = message;
        if (message.senderId == session->userId()) {
            data::ReceiptRepository receipts(m_database.handle());
            data::DeliveryState state = data::DeliveryState::Sent;
            if (receipts.stateForMessage(message.id, session->userId(), &state, &error))
                withState.state = state;
        }
        array.append(data::json::messageToJson(withState));
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("messages"), array);
    payload.insert(QStringLiteral("has_more"), hasMore);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleChatDirect(const QJsonObject &data, ClientSession *session)
{
    const QString username = stringValue(data, "username").trimmed();

    data::UserRepository users(m_database.handle());
    QString error;
    data::User peer;
    if (!users.findByUsername(username, &peer, &error))
        return HandlerResult::failure(core::ErrorCode::NotFound,
                                      QStringLiteral("Пользователь не найден"));

    if (peer.id == session->userId())
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Нельзя открыть чат с собой"));

    data::ChatRepository chats(m_database.handle());
    data::Chat chat;
    if (!chats.createDirect(session->userId(), peer.id, &chat, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    data::ChatSummary mine;
    if (!chats.summaryFor(chat.id, session->userId(), &mine, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    data::ChatSummary theirs;
    if (chats.summaryFor(chat.id, peer.id, &theirs, &error)) {
        QJsonObject event;
        event.insert(QStringLiteral("chat"), data::json::chatSummaryToJson(theirs));
        sendToUser(peer.id, core::proto::event::kChat, event, session);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("chat"), data::json::chatSummaryToJson(mine));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleChatRead(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const qint64 upToSeq = int64Value(data, "up_to_seq");

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    if (!chats.setLastRead(chatId, session->userId(), upToSeq, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
    event.insert(QStringLiteral("up_to_seq"), static_cast<double>(upToSeq));
    broadcastToChat(chatId, core::proto::event::kRead, event);

    return HandlerResult::success();
}

HandlerResult ServerCore::handleMessageSend(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const QString clientMsgId = stringValue(data, "client_msg_id");
    const QString kindText = stringValue(data, "kind");
    const QString body = stringValue(data, "body");
    const qint64 attachmentId = int64Value(data, "attachment_id");

    if (clientMsgId.isEmpty())
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Отсутствует client_msg_id"));

    bool kindOk = false;
    const data::MessageKind kind = data::messageKindFromString(kindText, &kindOk);
    if (!kindOk)
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Неизвестный тип сообщения"));

    const quint32 attachmentToken = static_cast<quint32>(attachmentId);

    if (kind == data::MessageKind::Text) {
        if (attachmentToken != 0) {
            return HandlerResult::failure(core::ErrorCode::BadRequest,
                                          QStringLiteral("Текстовое сообщение не содержит вложения"));
        }

        const security::CheckResult textCheck = security::checkMessageText(body);
        if (!textCheck.ok)
            return HandlerResult::failure(core::ErrorCode::BadRequest, textCheck.message);
    } else {
        if (attachmentToken == 0) {
            return HandlerResult::failure(core::ErrorCode::BadRequest,
                                          QStringLiteral("Не указано вложение"));
        }
        if (body.size() > core::limits::kMessageMaxChars) {
            return HandlerResult::failure(core::ErrorCode::BadRequest,
                                          QStringLiteral("Подпись слишком длинная"));
        }
    }

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    data::Message message;
    message.chatId = chatId;
    message.senderId = session->userId();
    message.clientMsgId = clientMsgId;
    message.kind = kind;
    message.body = body;
    message.createdAt = QDateTime::currentMSecsSinceEpoch();

    data::MessageRepository messages(m_database.handle());
    data::Message stored;
    if (!messages.append(message, &stored, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    if (attachmentToken != 0) {
        auto it = m_readyAttachments.find(attachmentToken);
        if (it == m_readyAttachments.end() || it->userId != session->userId()
            || it->consumed) {
            return HandlerResult::failure(core::ErrorCode::NotFound,
                                          QStringLiteral("Вложение не найдено"));
        }

        data::AttachmentRepository attachments(m_database.handle());
        data::Attachment existing;
        bool found = false;
        if (!attachments.byMessageId(stored.id, &existing, &found, &error))
            return HandlerResult::failure(core::ErrorCode::Internal, error);

        if (!found) {
            data::Attachment attachment;
            attachment.messageId = stored.id;
            attachment.fileName = it->fileName;
            attachment.mime = it->mime;
            attachment.byteSize = it->byteSize;
            attachment.sha256 = it->sha256;
            attachment.relPath = it->relPath;

            data::Attachment saved;
            if (!attachments.insert(attachment, &saved, &error))
                return HandlerResult::failure(core::ErrorCode::Internal, error);
        }

        it->consumed = true;
        m_readyAttachments.erase(it);
    }

    data::Message full;
    bool found = false;
    if (!messages.byId(stored.id, &full, &found, &error) || !found)
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonObject event;
    event.insert(QStringLiteral("message"), data::json::messageToJson(full));
    broadcastToChat(chatId, core::proto::event::kMsg, event, session);

    return HandlerResult::success(messageAck(stored));
}

HandlerResult ServerCore::handleMessageEdit(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const qint64 messageId = int64Value(data, "message_id");
    const QString body = stringValue(data, "body");

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    data::MessageRepository messages(m_database.handle());
    data::Message message;
    bool found = false;
    if (!messages.byId(messageId, &message, &found, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!found || message.chatId != chatId)
        return HandlerResult::failure(core::ErrorCode::NotFound);
    if (message.senderId != session->userId())
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Можно изменять только свои сообщения"));
    if (message.deleted)
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Сообщение удалено"));
    if (message.kind != data::MessageKind::Text)
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Можно изменять только текст"));

    const security::CheckResult textCheck = security::checkMessageText(body);
    if (!textCheck.ok)
        return HandlerResult::failure(core::ErrorCode::BadRequest, textCheck.message);

    if (!messages.updateEdited(messageId, body, QDateTime::currentMSecsSinceEpoch(), &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    data::Message full;
    if (!messages.byId(messageId, &full, &found, &error) || !found)
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("message"), data::json::messageToJson(full));
    broadcastToChat(chatId, core::proto::event::kMsgEdited, event, session);

    QJsonObject payload;
    payload.insert(QStringLiteral("message"), data::json::messageToJson(full));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleMessageDelete(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const qint64 messageId = int64Value(data, "message_id");

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    data::MessageRepository messages(m_database.handle());
    data::Message message;
    bool found = false;
    if (!messages.byId(messageId, &message, &found, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!found || message.chatId != chatId)
        return HandlerResult::failure(core::ErrorCode::NotFound);
    if (message.senderId != session->userId())
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Можно удалять только свои сообщения"));

    if (!messages.markDeleted(messageId, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    data::Message full;
    if (!messages.byId(messageId, &full, &found, &error) || !found)
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("message"), data::json::messageToJson(full));
    broadcastToChat(chatId, core::proto::event::kMsgDeleted, event, session);

    QJsonObject payload;
    payload.insert(QStringLiteral("message"), data::json::messageToJson(full));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleReceiptDelivered(const QJsonObject &data, ClientSession *session)
{
    const QJsonArray ids = data.value(QStringLiteral("message_ids")).toArray();
    if (ids.isEmpty())
        return HandlerResult::success();

    data::MessageRepository messages(m_database.handle());
    data::ChatRepository chats(m_database.handle());
    data::ReceiptRepository receipts(m_database.handle());

    QString error;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (const QJsonValue &value : ids) {
        const qint64 messageId = static_cast<qint64>(value.toDouble());

        data::Message message;
        bool found = false;
        if (!messages.byId(messageId, &message, &found, &error) || !found)
            continue;

        bool member = false;
        if (!chats.isMember(message.chatId, session->userId(), &member, &error) || !member)
            continue;

        if (message.senderId == session->userId())
            continue;

        if (!receipts.add(messageId, session->userId(), QStringLiteral("delivered"), now, &error))
            continue;

        QJsonObject event;
        event.insert(QStringLiteral("chat_id"), static_cast<double>(message.chatId));
        event.insert(QStringLiteral("message_id"), static_cast<double>(messageId));
        event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
        event.insert(QStringLiteral("state"), QStringLiteral("delivered"));
        sendToUser(message.senderId, core::proto::event::kReceipt, event, session);
    }

    return HandlerResult::success();
}

HandlerResult ServerCore::handleTyping(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const bool on = data.value(QStringLiteral("on")).toBool(false);

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
    event.insert(QStringLiteral("on"), on);
    broadcastToChat(chatId, core::proto::event::kTyping, event, session);

    return HandlerResult::success();
}

HandlerResult ServerCore::handleAttachBegin(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const QString fileName = stringValue(data, "file_name");
    const QString mime = stringValue(data, "mime");
    const qint64 byteSize = int64Value(data, "byte_size");
    const QString sha256 = stringValue(data, "sha256");

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    if (byteSize > core::limits::kAttachmentMaxBytes)
        return HandlerResult::failure(core::ErrorCode::TooLarge);

    int chunkSize = 0;
    const quint32 transferId = m_attachmentStore.begin(
        session->userId(), fileName, mime, byteSize, sha256, &chunkSize, &error);

    if (transferId == 0)
        return HandlerResult::failure(core::ErrorCode::BadRequest, error);

    QJsonObject payload;
    payload.insert(QStringLiteral("transfer_id"), static_cast<double>(transferId));
    payload.insert(QStringLiteral("chunk_size"), chunkSize);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleAttachFinish(const QJsonObject &data, ClientSession *session)
{
    const quint32 transferId = static_cast<quint32>(int64Value(data, "transfer_id"));

    ReadyAttachment ready;
    QString error;
    if (!m_attachmentStore.finish(session->userId(), transferId, &ready, &error))
        return HandlerResult::failure(core::ErrorCode::BadRequest, error);

    m_readyAttachments.insert(transferId, ready);

    QJsonObject payload;
    payload.insert(QStringLiteral("attachment_id"), static_cast<double>(transferId));
    payload.insert(QStringLiteral("byte_size"), static_cast<double>(ready.byteSize));
    payload.insert(QStringLiteral("sha256"), ready.sha256);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleAttachGet(const QJsonObject &data, ClientSession *session)
{
    const qint64 attachmentId = int64Value(data, "attachment_id");

    data::AttachmentRepository attachments(m_database.handle());
    data::Attachment attachment;
    bool found = false;
    QString error;

    if (!attachments.byId(attachmentId, &attachment, &found, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!found)
        return HandlerResult::failure(core::ErrorCode::NotFound);

    data::MessageRepository messages(m_database.handle());
    data::Message message;
    if (!messages.byId(attachment.messageId, &message, &found, &error) || !found)
        return HandlerResult::failure(core::ErrorCode::NotFound);

    data::ChatRepository chats(m_database.handle());
    bool member = false;
    if (!chats.isMember(message.chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    const QString path = m_attachmentStore.absolutePath(attachment.relPath);
    if (!m_attachmentStore.contains(attachment.relPath))
        return HandlerResult::failure(core::ErrorCode::NotFound,
                                      QStringLiteral("Файл вложения отсутствует"));

    const quint32 transferId = session->allocateDownloadId();
    session->queueDownload(transferId, path, attachment.byteSize);

    QJsonObject payload;
    payload.insert(QStringLiteral("transfer_id"), static_cast<double>(transferId));
    payload.insert(QStringLiteral("byte_size"), static_cast<double>(attachment.byteSize));
    payload.insert(QStringLiteral("sha256"), attachment.sha256);
    payload.insert(QStringLiteral("chunk_size"),
                   static_cast<double>(core::limits::kChunkSizeBytes));
    return HandlerResult::success(payload);
}

bool ServerCore::handleChunk(ClientSession *session, quint32 transferId, quint32 sequence,
                             const QByteArray &data, QString *error)
{
    if (!session || !session->isAuthenticated()) {
        if (error)
            *error = QStringLiteral("Чанк получен до авторизации");
        return false;
    }

    return m_attachmentStore.writeChunk(session->userId(), transferId, sequence, data, error);
}

QString ServerCore::issueToken(qint64 userId, QString *error)
{
    const QString token = core::newToken(32);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 expires = now + qint64(kTokenTtlDays) * 24 * 60 * 60 * 1000;

    QSqlQuery cleanup(m_database.handle());
    cleanup.prepare(QStringLiteral(
        "DELETE FROM sessions WHERE expires_at <= ? OR user_id = ? AND token = ''"));
    cleanup.addBindValue(now);
    cleanup.addBindValue(userId);
    cleanup.exec();

    QSqlQuery query(m_database.handle());
    query.prepare(QStringLiteral(
        "INSERT INTO sessions(token, user_id, created_at, last_seen, expires_at) "
        "VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(token);
    query.addBindValue(userId);
    query.addBindValue(now);
    query.addBindValue(now);
    query.addBindValue(expires);

    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return QString();
    }
    return token;
}

bool ServerCore::resolveToken(const QString &token, qint64 *userId, QString *error)
{
    QSqlQuery query(m_database.handle());
    query.prepare(QStringLiteral(
        "SELECT user_id FROM sessions WHERE token = ? AND expires_at > ?"));
    query.addBindValue(token);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        if (error)
            *error = query.lastError().text();
        return false;
    }

    if (!query.next())
        return false;

    const qint64 id = query.value(0).toLongLong();

    QSqlQuery touch(m_database.handle());
    touch.prepare(QStringLiteral("UPDATE sessions SET last_seen = ? WHERE token = ?"));
    touch.addBindValue(QDateTime::currentMSecsSinceEpoch());
    touch.addBindValue(token);
    touch.exec();

    if (userId)
        *userId = id;
    return true;
}

void ServerCore::dropToken(const QString &token)
{
    if (token.isEmpty())
        return;

    QSqlQuery query(m_database.handle());
    query.prepare(QStringLiteral("DELETE FROM sessions WHERE token = ?"));
    query.addBindValue(token);
    query.exec();
}

void ServerCore::registerSession(ClientSession *session)
{
    if (session && session->userId() > 0)
        m_sessions.insert(session->userId(), session);
}

void ServerCore::unregisterSession(ClientSession *session)
{
    if (!session)
        return;

    const qint64 userId = session->userId();
    m_sessions.remove(userId, session);

    if (userId > 0 && !m_sessions.contains(userId)) {
        // Незавершённые загрузки и непривязанные файлы убираем вместе с сессией.
        m_attachmentStore.abortUser(userId);

        const auto tokens = m_readyAttachments.keys();
        for (quint32 token : tokens) {
            if (m_readyAttachments.value(token).userId == userId)
                m_readyAttachments.remove(token);
        }

        broadcastPresence(userId, false);
    }
}

void ServerCore::broadcastToChat(qint64 chatId, const QString &event, const QJsonObject &data,
                                 ClientSession *skip)
{
    data::ChatRepository chats(m_database.handle());
    QString error;
    QList<qint64> members;
    if (!chats.memberIds(chatId, &members, &error))
        return;

    const QJsonObject object = core::proto::eventObject(event, data);
    for (const qint64 memberId : members)
        deliver(memberId, object, skip);
}

void ServerCore::sendToUser(qint64 userId, const QString &event, const QJsonObject &data,
                            ClientSession *skip)
{
    deliver(userId, core::proto::eventObject(event, data), skip);
}

void ServerCore::deliver(qint64 userId, const QJsonObject &object, ClientSession *skip)
{
    const auto range = m_sessions.equal_range(userId);
    for (auto it = range.first; it != range.second; ++it) {
        ClientSession *session = it.value();
        if (session && session != skip)
            session->sendJson(object);
    }
}

void ServerCore::broadcastPresence(qint64 userId, bool online)
{
    data::UserRepository users(m_database.handle());
    QString error;
    if (!users.findById(userId, nullptr, &error))
        return;

    QJsonObject event;
    event.insert(QStringLiteral("user_id"), static_cast<double>(userId));
    event.insert(QStringLiteral("online"), online);
    event.insert(QStringLiteral("last_seen"),
                 static_cast<double>(QDateTime::currentMSecsSinceEpoch()));

    const QJsonObject object = core::proto::eventObject(core::proto::event::kPresence, event);
    for (ClientSession *session : qAsConst(m_sessions)) {
        if (session->userId() != userId)
            session->sendJson(object);
    }
}

} // namespace server
