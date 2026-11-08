#include "server/ServerCore.h"

#include "core/Ids.h"
#include "core/Limits.h"
#include "core/Protocol.h"
#include "data/AttachmentRepository.h"
#include "data/ChatRepository.h"
#include "data/JsonMapping.h"
#include "data/MessageRepository.h"
#include "data/PromoRepository.h"
#include "data/ReceiptRepository.h"
#include "data/SubscriptionRepository.h"
#include "data/UserRepository.h"
#include "security/PasswordHasher.h"
#include "security/Validator.h"
#include "server/ClientSession.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QUdpSocket>
#include <QVariant>
#include <QVector>

namespace server {
namespace {

// Пакеты оформления. Базовые токены совпадают с ThemeManager клиента,
// поэтому пакет может переопределять только нужные.
struct ThemePack {
    QString id;
    QString name;
    bool premium = false;
    QString accent;
    QHash<QString, QString> tokens;
};

const QVector<ThemePack> &themePacks()
{
    static const QVector<ThemePack> packs = {
        {QStringLiteral("midnight"), QStringLiteral("Полночь"), true, QStringLiteral("#6C8CFF"),
         {
             {QStringLiteral("bg"), QStringLiteral("#0B1220")},
             {QStringLiteral("surface"), QStringLiteral("#121B2E")},
             {QStringLiteral("surface-2"), QStringLiteral("#18243B")},
             {QStringLiteral("surface-3"), QStringLiteral("#1F2D47")},
             {QStringLiteral("border"), QStringLiteral("#24324D")},
             {QStringLiteral("border-strong"), QStringLiteral("#2E3F5E")},
             {QStringLiteral("text"), QStringLiteral("#E6EDF7")},
             {QStringLiteral("text-muted"), QStringLiteral("#93A4BF")},
             {QStringLiteral("text-faint"), QStringLiteral("#6B7C99")},
             {QStringLiteral("hover"), QStringLiteral("#17233A")},
             {QStringLiteral("selected"), QStringLiteral("#1E2E4F")},
             {QStringLiteral("accent"), QStringLiteral("#6C8CFF")},
             {QStringLiteral("accent-hover"), QStringLiteral("#8AA4FF")},
             {QStringLiteral("accent-pressed"), QStringLiteral("#5B78DB")},
             {QStringLiteral("accent-soft"), QStringLiteral("#1B2A45")},
             {QStringLiteral("scroll"), QStringLiteral("#2A3B58")},
             {QStringLiteral("scroll-hover"), QStringLiteral("#3A4E70")},
         }},
        {QStringLiteral("sand"), QStringLiteral("Песок"), true, QStringLiteral("#B7791F"),
         {
             {QStringLiteral("bg"), QStringLiteral("#F7F2E8")},
             {QStringLiteral("surface"), QStringLiteral("#FFFDF8")},
             {QStringLiteral("surface-2"), QStringLiteral("#EFE7D8")},
             {QStringLiteral("surface-3"), QStringLiteral("#E5DAC7")},
             {QStringLiteral("border"), QStringLiteral("#DED2BE")},
             {QStringLiteral("border-strong"), QStringLiteral("#CDBEA4")},
             {QStringLiteral("text"), QStringLiteral("#2B2620")},
             {QStringLiteral("text-muted"), QStringLiteral("#7A6E5C")},
             {QStringLiteral("text-faint"), QStringLiteral("#9C8E78")},
             {QStringLiteral("hover"), QStringLiteral("#F0E8DA")},
             {QStringLiteral("selected"), QStringLiteral("#F3E4C8")},
             {QStringLiteral("accent"), QStringLiteral("#B7791F")},
             {QStringLiteral("accent-hover"), QStringLiteral("#D08A2A")},
             {QStringLiteral("accent-pressed"), QStringLiteral("#96620F")},
             {QStringLiteral("accent-soft"), QStringLiteral("#F6E7CB")},
             {QStringLiteral("scroll"), QStringLiteral("#D8C8AE")},
             {QStringLiteral("scroll-hover"), QStringLiteral("#C4B08F")},
         }},
    };
    return packs;
}

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

    if (m_updatesRoot.isEmpty()) {
        m_updatesRoot = QFileInfo(attachmentsRoot).absolutePath()
                        + QStringLiteral("/updates");
    }

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
    if (type == QLatin1String(core::proto::type::kChatCreate))
        return handleChatCreate(data, session);
    if (type == QLatin1String(core::proto::type::kChatMembers))
        return handleChatMembers(data, session);
    if (type == QLatin1String(core::proto::type::kChatAdd))
        return handleChatAdd(data, session);
    if (type == QLatin1String(core::proto::type::kChatLeave))
        return handleChatLeave(data, session);
    if (type == QLatin1String(core::proto::type::kUserSearch))
        return handleUserSearch(data, session);
    if (type == QLatin1String(core::proto::type::kBillingStatus))
        return handleBillingStatus(session);
    if (type == QLatin1String(core::proto::type::kBillingRedeem))
        return handleBillingRedeem(data, session);
    if (type == QLatin1String(core::proto::type::kThemeList))
        return handleThemeList(session);
    if (type == QLatin1String(core::proto::type::kThemeGet))
        return handleThemeGet(data, session);
    if (type == QLatin1String(core::proto::type::kVoiceJoin))
        return handleVoiceJoin(data, session);
    if (type == QLatin1String(core::proto::type::kVoiceLeave))
        return handleVoiceLeave(data, session);
    if (type == QLatin1String(core::proto::type::kVoiceInvite))
        return handleVoiceInvite(data, session);
    if (type == QLatin1String(core::proto::type::kVoiceAccept))
        return handleVoiceAccept(data, session);
    if (type == QLatin1String(core::proto::type::kVoiceDecline))
        return handleVoiceDecline(data, session);
    if (type == QLatin1String(core::proto::type::kVoiceCancel))
        return handleVoiceCancel(data, session);
    if (type == QLatin1String(core::proto::type::kVoiceSpeaking))
        return handleVoiceSpeaking(data, session);
    if (type == QLatin1String(core::proto::type::kUpdateManifest))
        return handleUpdateManifest(session);
    if (type == QLatin1String(core::proto::type::kUpdateDownload))
        return handleUpdateDownload(data, session);

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

HandlerResult ServerCore::handleChatCreate(const QJsonObject &data, ClientSession *session)
{
    const QString title = stringValue(data, "title").trimmed();
    if (title.size() < 2 || title.size() > 64)
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Название группы: от 2 до 64 символов"));

    QSet<qint64> memberIds;
    const QJsonArray array = data.value(QStringLiteral("member_ids")).toArray();
    for (const QJsonValue &value : array) {
        const qint64 id = static_cast<qint64>(value.toDouble());
        if (id > 0 && id != session->userId())
            memberIds.insert(id);
    }

    if (memberIds.size() > 50)
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Слишком много участников"));

    data::UserRepository users(m_database.handle());
    QString error;
    data::User member;
    for (const qint64 id : memberIds) {
        if (!users.findById(id, &member, &error))
            return HandlerResult::failure(core::ErrorCode::NotFound,
                                          QStringLiteral("Пользователь не найден"));
    }

    data::ChatRepository chats(m_database.handle());
    data::Chat created;
    if (!chats.createGroup(title, &created, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    if (!chats.addMember(created.id, session->userId(), QStringLiteral("owner"), &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    for (const qint64 id : memberIds) {
        if (!chats.addMember(created.id, id, QStringLiteral("member"), &error))
            return HandlerResult::failure(core::ErrorCode::Internal, error);
    }

    // Приглашённым отправляем событие, чтобы чат появился в списке.
    for (const qint64 id : memberIds) {
        data::ChatSummary theirs;
        if (!chats.summaryFor(created.id, id, &theirs, &error))
            continue;

        QJsonObject event;
        event.insert(QStringLiteral("chat"), data::json::chatSummaryToJson(theirs));
        sendToUser(id, core::proto::event::kChat, event, session);
    }

    data::ChatSummary mine;
    if (!chats.summaryFor(created.id, session->userId(), &mine, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonObject payload;
    payload.insert(QStringLiteral("chat"), data::json::chatSummaryToJson(mine));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleChatMembers(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    QList<data::ChatMember> members;
    if (!chats.members(chatId, &members, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonArray array;
    for (data::ChatMember &entry : members) {
        entry.online = isUserOnline(entry.userId);
        array.append(data::json::chatMemberToJson(entry));
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("members"), array);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleChatAdd(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");

    data::ChatRepository chats(m_database.handle());
    QString error;
    QString role;
    bool found = false;
    if (!chats.memberRole(chatId, session->userId(), &role, &found, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!found)
        return HandlerResult::failure(core::ErrorCode::NotMember);
    if (role != QLatin1String("owner") && role != QLatin1String("admin"))
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Недостаточно прав"));

    QSet<qint64> userIds;
    const QJsonArray array = data.value(QStringLiteral("user_ids")).toArray();
    for (const QJsonValue &value : array) {
        const qint64 id = static_cast<qint64>(value.toDouble());
        if (id > 0 && id != session->userId())
            userIds.insert(id);
    }
    if (userIds.isEmpty())
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Не выбраны участники"));

    data::UserRepository users(m_database.handle());
    data::User user;
    for (const qint64 id : userIds) {
        if (!users.findById(id, &user, &error))
            return HandlerResult::failure(core::ErrorCode::NotFound,
                                          QStringLiteral("Пользователь не найден"));
        if (!chats.addMember(chatId, id, QStringLiteral("member"), &error))
            return HandlerResult::failure(core::ErrorCode::Internal, error);

        data::ChatSummary summary;
        if (chats.summaryFor(chatId, id, &summary, &error)) {
            QJsonObject event;
            event.insert(QStringLiteral("chat"), data::json::chatSummaryToJson(summary));
            sendToUser(id, core::proto::event::kChat, event, session);
        }
    }

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    broadcastToChat(chatId, core::proto::event::kChatMembers, event);

    return HandlerResult::success();
}

HandlerResult ServerCore::handleChatLeave(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");

    data::ChatRepository chats(m_database.handle());
    QString error;
    QString role;
    bool found = false;
    if (!chats.memberRole(chatId, session->userId(), &role, &found, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!found)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    QList<data::ChatMember> members;
    if (!chats.members(chatId, &members, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    // Владелец передаёт роль следующему участнику.
    if (role == QLatin1String("owner")) {
        for (const data::ChatMember &member : members) {
            if (member.userId == session->userId())
                continue;
            if (!chats.setRole(chatId, member.userId, QStringLiteral("owner"), &error))
                return HandlerResult::failure(core::ErrorCode::Internal, error);
            break;
        }
    }

    if (!chats.removeMember(chatId, session->userId(), &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QList<data::ChatMember> remaining;
    if (!chats.members(chatId, &remaining, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    if (remaining.isEmpty()) {
        if (!chats.deleteChat(chatId, &error))
            return HandlerResult::failure(core::ErrorCode::Internal, error);
    } else {
        QJsonObject event;
        event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
        broadcastToChat(chatId, core::proto::event::kChatMembers, event);
    }

    return HandlerResult::success();
}

HandlerResult ServerCore::handleUserSearch(const QJsonObject &data, ClientSession *session)
{
    const QString query = stringValue(data, "query").trimmed();
    const int limit = qBound(1, data.value(QStringLiteral("limit")).toInt(20), 50);

    data::UserRepository users(m_database.handle());
    QList<data::User> found;
    QString error;
    if (!users.search(query, session->userId(), limit, &found, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonArray array;
    for (const data::User &user : found)
        array.append(data::json::userToJson(user));

    QJsonObject payload;
    payload.insert(QStringLiteral("users"), array);
    return HandlerResult::success(payload);
}

bool ServerCore::isUserOnline(qint64 userId) const
{
    return m_sessions.contains(userId);
}

bool ServerCore::createPromo(const QString &code, const QString &plan, int days, int maxUses,
                             QString *error)
{
    data::PromoRepository promos(m_database.handle());
    return promos.create(code, plan, days, maxUses, 0, error);
}

HandlerResult ServerCore::handleBillingStatus(ClientSession *session)
{
    data::SubscriptionRepository subscriptions(m_database.handle());
    QString plan;
    qint64 expiresAt = 0;
    QString error;

    if (!subscriptions.current(session->userId(), &plan, &expiresAt, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonArray entitlements;
    if (plan != QLatin1String("free"))
        entitlements.append(QStringLiteral("themes.premium"));

    QJsonObject payload;
    payload.insert(QStringLiteral("plan"), plan);
    payload.insert(QStringLiteral("expires_at"), static_cast<double>(expiresAt));
    payload.insert(QStringLiteral("entitlements"), entitlements);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleBillingRedeem(const QJsonObject &data, ClientSession *session)
{
    const QString code = stringValue(data, "code");

    data::PromoRepository promos(m_database.handle());
    QString plan;
    int days = 0;
    QString error;

    if (!promos.redeem(code, session->userId(), &plan, &days, &error))
        return HandlerResult::failure(core::ErrorCode::BadRequest, error);

    data::SubscriptionRepository subscriptions(m_database.handle());
    qint64 expiresAt = 0;
    if (!subscriptions.activate(session->userId(), plan, days, &expiresAt, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    qInfo().noquote() << QStringLiteral("Промокод %1 активирован для #%2 до %3")
                             .arg(code, QString::number(session->userId()),
                                  QDateTime::fromMSecsSinceEpoch(expiresAt).toString(
                                      QStringLiteral("yyyy-MM-dd")));

    QJsonObject payload;
    payload.insert(QStringLiteral("plan"), plan);
    payload.insert(QStringLiteral("expires_at"), static_cast<double>(expiresAt));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleThemeList(ClientSession *session)
{
    Q_UNUSED(session)

    QJsonArray array;
    for (const ThemePack &pack : themePacks()) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), pack.id);
        item.insert(QStringLiteral("name"), pack.name);
        item.insert(QStringLiteral("premium"), pack.premium);
        item.insert(QStringLiteral("accent"), pack.accent);
        array.append(item);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("themes"), array);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleThemeGet(const QJsonObject &data, ClientSession *session)
{
    const QString id = stringValue(data, "id");

    const ThemePack *found = nullptr;
    for (const ThemePack &pack : themePacks()) {
        if (pack.id == id) {
            found = &pack;
            break;
        }
    }

    if (!found)
        return HandlerResult::failure(core::ErrorCode::NotFound,
                                      QStringLiteral("Тема не найдена"));

    if (found->premium) {
        data::SubscriptionRepository subscriptions(m_database.handle());
        QString plan;
        qint64 expiresAt = 0;
        QString error;
        if (!subscriptions.current(session->userId(), &plan, &expiresAt, &error))
            return HandlerResult::failure(core::ErrorCode::Internal, error);
        if (plan == QLatin1String("free"))
            return HandlerResult::failure(core::ErrorCode::BadRequest,
                                          QStringLiteral("Требуется подписка"));
    }

    QJsonObject tokens;
    for (auto it = found->tokens.constBegin(); it != found->tokens.constEnd(); ++it)
        tokens.insert(it.key(), it.value());

    QJsonObject theme;
    theme.insert(QStringLiteral("id"), found->id);
    theme.insert(QStringLiteral("name"), found->name);
    theme.insert(QStringLiteral("premium"), found->premium);
    theme.insert(QStringLiteral("tokens"), tokens);

    QJsonObject payload;
    payload.insert(QStringLiteral("theme"), theme);
    return HandlerResult::success(payload);
}

bool ServerCore::startVoiceSocket(quint16 port, QString *error)
{
    if (!m_voiceSocket) {
        m_voiceSocket = new QUdpSocket(this);
        connect(m_voiceSocket, &QUdpSocket::readyRead, this, &ServerCore::onVoiceDatagram);
    }

    if (m_voiceSocket->state() == QAbstractSocket::BoundState)
        return true;

    if (!m_voiceSocket->bind(QHostAddress::Any, port)) {
        if (error)
            *error = QStringLiteral("Не удалось открыть UDP-порт %1: %2")
                         .arg(port)
                         .arg(m_voiceSocket->errorString());
        return false;
    }

    m_voicePort = m_voiceSocket->localPort();
    qInfo().noquote() << QStringLiteral("Голосовой UDP-релей слушает 0.0.0.0:%1")
                             .arg(m_voicePort);
    return true;
}

void ServerCore::stopVoiceSocket()
{
    if (!m_voiceSocket)
        return;

    m_voiceSocket->close();
    m_voiceSocket->deleteLater();
    m_voiceSocket = nullptr;
    m_voiceEndpoints.clear();
    m_sessionVoiceTokens.clear();
}

void ServerCore::removeVoiceEndpoint(ClientSession *session)
{
    const auto it = m_sessionVoiceTokens.find(session);
    if (it == m_sessionVoiceTokens.end())
        return;

    m_voiceEndpoints.remove(it.value());
    m_sessionVoiceTokens.erase(it);
}

void ServerCore::onVoiceDatagram()
{
    while (m_voiceSocket && m_voiceSocket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_voiceSocket->receiveDatagram();
        if (datagram.data().size() < 12 || datagram.data().size() > 4096)
            continue;

        quint32 token = 0;
        quint32 sequence = 0;
        QByteArray payload;
        if (!core::proto::decodeVoiceDatagram(datagram.data(), &token, &sequence, &payload))
            continue;

        auto endpointIt = m_voiceEndpoints.find(token);
        if (endpointIt == m_voiceEndpoints.end())
            continue;

        // Запоминаем адрес клиента: это открывает NAT-путь для обратных пакетов.
        // Пустой payload — «привет»/keepalive, только регистрирует адрес.
        endpointIt->address = datagram.senderAddress();
        endpointIt->port = static_cast<quint16>(datagram.senderPort());

        if (payload.isEmpty())
            continue;

        const VoiceEndpoint sender = endpointIt.value();
        const QByteArray relay =
            core::proto::encodeVoiceRelay(static_cast<quint32>(sender.userId), sequence, payload);

        for (auto it = m_voiceEndpoints.constBegin(); it != m_voiceEndpoints.constEnd(); ++it) {
            if (it.key() == token || it.value().chatId != sender.chatId)
                continue;
            if (it.value().address.isNull() || it.value().port == 0)
                continue;

            m_voiceSocket->writeDatagram(relay, it.value().address, it.value().port);
        }
    }
}

HandlerResult ServerCore::handleVoiceInvite(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    data::UserRepository users(m_database.handle());
    data::User caller;
    if (!users.findById(session->userId(), &caller, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("caller_id"), static_cast<double>(session->userId()));
    event.insert(QStringLiteral("caller_name"), caller.displayName);
    broadcastToChat(chatId, core::proto::event::kVoiceInvite, event, session);

    return HandlerResult::success();
}

HandlerResult ServerCore::handleVoiceAccept(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const qint64 callerId = int64Value(data, "caller_id");

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
    sendToUser(callerId, core::proto::event::kVoiceAccepted, event);

    return HandlerResult::success();
}

HandlerResult ServerCore::handleVoiceDecline(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");
    const qint64 callerId = int64Value(data, "caller_id");

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
    sendToUser(callerId, core::proto::event::kVoiceDeclined, event);

    return HandlerResult::success();
}

HandlerResult ServerCore::handleVoiceCancel(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("caller_id"), static_cast<double>(session->userId()));
    broadcastToChat(chatId, core::proto::event::kVoiceCancelled, event, session);

    return HandlerResult::success();
}

HandlerResult ServerCore::handleVoiceSpeaking(const QJsonObject &data, ClientSession *session)
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
    broadcastToChat(chatId, core::proto::event::kVoiceSpeaking, event, session);

    return HandlerResult::success();
}

bool ServerCore::loadUpdateManifest(QString *version, QList<UpdateFile> *files,
                                    QString *error) const
{
    if (m_updatesRoot.isEmpty()) {
        if (error)
            *error = QStringLiteral("Каталог обновлений не задан");
        return false;
    }

    QFile manifest(m_updatesRoot + QStringLiteral("/manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Манифест обновлений не найден");
        return false;
    }

    const QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
    const QString manifestVersion = object.value(QStringLiteral("version")).toString();
    if (manifestVersion.isEmpty()) {
        if (error)
            *error = QStringLiteral("В манифесте нет поля version");
        return false;
    }

    QList<UpdateFile> parsed;
    const QJsonArray array = object.value(QStringLiteral("files")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject item = value.toObject();
        const QString path = item.value(QStringLiteral("path")).toString();

        if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1String(".."))
            || path.contains(QLatin1Char(':')) || path.contains(QLatin1Char('\\'))) {
            if (error)
                *error = QStringLiteral("Недопустимый путь в манифесте: %1").arg(path);
            return false;
        }

        UpdateFile file;
        file.path = path;
        file.size = static_cast<qint64>(item.value(QStringLiteral("size")).toDouble());
        file.sha256 = item.value(QStringLiteral("sha256")).toString();
        if (file.size <= 0 || file.sha256.isEmpty()) {
            if (error)
                *error = QStringLiteral("Некорректная запись манифеста: %1").arg(path);
            return false;
        }
        parsed.append(file);
    }

    if (version)
        *version = manifestVersion;
    if (files)
        *files = parsed;
    return true;
}

HandlerResult ServerCore::handleUpdateManifest(ClientSession *session)
{
    Q_UNUSED(session)

    QString version;
    QList<UpdateFile> files;
    QString error;
    if (!loadUpdateManifest(&version, &files, &error))
        return HandlerResult::failure(core::ErrorCode::NotFound, error);

    QJsonArray array;
    for (const UpdateFile &file : files) {
        QJsonObject item;
        item.insert(QStringLiteral("path"), file.path);
        item.insert(QStringLiteral("size"), static_cast<double>(file.size));
        item.insert(QStringLiteral("sha256"), file.sha256);
        array.append(item);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("version"), version);
    payload.insert(QStringLiteral("files"), array);
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleUpdateDownload(const QJsonObject &data, ClientSession *session)
{
    const QString path = stringValue(data, "path");
    const qint64 offset = qMax<qint64>(0, int64Value(data, "offset"));

    QString version;
    QList<UpdateFile> files;
    QString error;
    if (!loadUpdateManifest(&version, &files, &error))
        return HandlerResult::failure(core::ErrorCode::NotFound, error);

    UpdateFile target;
    bool found = false;
    for (const UpdateFile &file : files) {
        if (file.path == path) {
            target = file;
            found = true;
            break;
        }
    }

    if (!found)
        return HandlerResult::failure(core::ErrorCode::NotFound,
                                      QStringLiteral("Файл отсутствует в манифесте"));
    if (offset > target.size)
        return HandlerResult::failure(core::ErrorCode::BadRequest,
                                      QStringLiteral("Смещение больше размера файла"));

    const QString absolute = m_updatesRoot + QLatin1Char('/') + target.path;
    const QFileInfo info(absolute);
    if (!info.exists() || info.size() != target.size)
        return HandlerResult::failure(core::ErrorCode::NotFound,
                                      QStringLiteral("Файл обновления недоступен"));

    const quint32 transferId = session->allocateDownloadId();
    session->queueDownload(transferId, absolute, target.size, offset);

    QJsonObject payload;
    payload.insert(QStringLiteral("transfer_id"), static_cast<double>(transferId));
    payload.insert(QStringLiteral("size"), static_cast<double>(target.size));
    payload.insert(QStringLiteral("sha256"), target.sha256);
    payload.insert(QStringLiteral("offset"), static_cast<double>(offset));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleVoiceJoin(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");

    data::ChatRepository chats(m_database.handle());
    QString error;
    bool member = false;
    if (!chats.isMember(chatId, session->userId(), &member, &error))
        return HandlerResult::failure(core::ErrorCode::Internal, error);
    if (!member)
        return HandlerResult::failure(core::ErrorCode::NotMember);

    m_voiceRooms[chatId].insert(session);

    // Токен для UDP-релея: клиент шлёт его в каждом дейтаграмме.
    removeVoiceEndpoint(session);
    quint32 voiceToken = 0;
    do {
        voiceToken = QRandomGenerator::global()->generate();
    } while (voiceToken == 0 || m_voiceEndpoints.contains(voiceToken));

    VoiceEndpoint endpoint;
    endpoint.chatId = chatId;
    endpoint.userId = session->userId();
    m_voiceEndpoints.insert(voiceToken, endpoint);
    m_sessionVoiceTokens.insert(session, voiceToken);

    QJsonObject event;
    event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
    event.insert(QStringLiteral("joined"), true);
    broadcastToChat(chatId, core::proto::event::kVoice, event);

    QJsonObject payload;
    payload.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    payload.insert(QStringLiteral("codec"), QStringLiteral("opus"));
    payload.insert(QStringLiteral("sample_rate"), 48000);
    payload.insert(QStringLiteral("frame_ms"), 20);
    payload.insert(QStringLiteral("udp_port"), m_voicePort);
    payload.insert(QStringLiteral("udp_token"), static_cast<double>(voiceToken));
    return HandlerResult::success(payload);
}

HandlerResult ServerCore::handleVoiceLeave(const QJsonObject &data, ClientSession *session)
{
    const qint64 chatId = int64Value(data, "chat_id");

    removeVoiceEndpoint(session);

    auto it = m_voiceRooms.find(chatId);
    if (it != m_voiceRooms.end() && it.value().remove(session)) {
        if (it.value().isEmpty())
            m_voiceRooms.erase(it);

        QJsonObject event;
        event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
        event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
        event.insert(QStringLiteral("joined"), false);
        broadcastToChat(chatId, core::proto::event::kVoice, event);
    }

    return HandlerResult::success();
}

void ServerCore::leaveVoiceRoom(ClientSession *session)
{
    if (!session)
        return;

    const auto chatIds = m_voiceRooms.keys();
    for (const qint64 chatId : chatIds) {
        auto it = m_voiceRooms.find(chatId);
        if (it == m_voiceRooms.end() || !it.value().contains(session))
            continue;

        it.value().remove(session);
        if (it.value().isEmpty())
            m_voiceRooms.erase(it);

        QJsonObject event;
        event.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
        event.insert(QStringLiteral("user_id"), static_cast<double>(session->userId()));
        event.insert(QStringLiteral("joined"), false);
        broadcastToChat(chatId, core::proto::event::kVoice, event);
    }
}

void ServerCore::handleVoiceFrame(ClientSession *session, qint64 chatId, quint32 sequence,
                                  const QByteArray &data)
{
    if (!session || data.isEmpty())
        return;

    auto it = m_voiceRooms.find(chatId);
    if (it == m_voiceRooms.end() || !it.value().contains(session))
        return;

    const QSet<ClientSession *> listeners = it.value();
    for (ClientSession *peer : listeners) {
        if (peer != session)
            peer->sendVoiceFrame(static_cast<quint32>(chatId),
                                 static_cast<quint32>(session->userId()), sequence, data);
    }
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

    leaveVoiceRoom(session);
    removeVoiceEndpoint(session);

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
