#include "app/ChatService.h"

#include "app/HistoryStore.h"
#include "core/Ids.h"
#include "core/Limits.h"
#include "core/Protocol.h"
#include "data/JsonMapping.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>

namespace app {
namespace {

QJsonObject replyData(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("data")).toObject();
}

bool replyOk(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("ok")).toBool(false);
}

} // namespace

ChatService::ChatService(net::ClientConnection *connection, HistoryStore *history,
                         QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_history(history)
{
    connect(m_connection, &net::ClientConnection::eventReceived,
            this, &ChatService::onEvent);

    if (!m_history)
        return;

    connect(m_connection, &net::ClientConnection::socketConnected, this,
            [this]() { flushOutbox(); });

    connect(m_history, &HistoryStore::chatsLoaded, this,
            [this](const QList<data::ChatSummary> &chats) {
                // Кэш показывает интерфейс сразу; сетевой список его заменит.
                if (!m_chats.isEmpty() || chats.isEmpty())
                    return;
                m_chats = chats;
                emit chatsChanged();
            });

    connect(m_history, &HistoryStore::messagesLoaded, this,
            [this](qint64 chatId, const QList<data::Message> &messages) {
                ChatState &state = m_states[chatId];
                if (state.loaded || messages.isEmpty())
                    return;
                state.messages = messages;
                state.loaded = true;
                state.hasMore = messages.size() >= core::limits::kHistoryPageSize;
                emit messagesChanged(chatId);
            });

    connect(m_history, &HistoryStore::outboxLoaded, this,
            [this](const QList<data::OutboxEntry> &entries) {
                for (const data::OutboxEntry &entry : entries) {
                    ChatState &state = m_states[entry.chatId];

                    data::Message pending;
                    for (const data::Message &message : qAsConst(state.messages)) {
                        if (message.clientMsgId == entry.clientMsgId) {
                            pending = message;
                            break;
                        }
                    }

                    if (pending.clientMsgId.isEmpty()) {
                        pending.chatId = entry.chatId;
                        pending.senderId = m_currentUserId;
                        pending.clientMsgId = entry.clientMsgId;
                        pending.kind = entry.kind;
                        pending.body = entry.body;
                        pending.createdAt = entry.createdAt;
                        pending.state = data::DeliveryState::Pending;
                        pending.senderDisplayName = m_currentUser.displayName;
                        pending.senderHue = m_currentUser.avatarHue;
                        state.messages.append(pending);
                        emit messagesChanged(entry.chatId);
                    }

                    sendPending(pending);
                }
            });
}

void ChatService::setCurrentUser(const data::User &user)
{
    m_currentUser = user;
    m_currentUserId = user.id;

    if (m_history)
        m_history->loadChats();
}

data::ChatSummary ChatService::chat(qint64 chatId) const
{
    const int index = indexOfChat(chatId);
    return index >= 0 ? m_chats.at(index) : data::ChatSummary();
}

int ChatService::indexOfChat(qint64 chatId) const
{
    for (int i = 0; i < m_chats.size(); ++i) {
        if (m_chats.at(i).chat.id == chatId)
            return i;
    }
    return -1;
}

QList<data::Message> ChatService::messages(qint64 chatId) const
{
    return m_states.value(chatId).messages;
}

bool ChatService::hasMore(qint64 chatId) const
{
    return m_states.value(chatId).hasMore;
}

bool ChatService::isLoaded(qint64 chatId) const
{
    return m_states.value(chatId).loaded;
}

void ChatService::loadChats()
{
    m_connection->request(QStringLiteral("chat.list"), QJsonObject(),
                          [this](const QJsonObject &reply) {
                              if (!replyOk(reply)) {
                                  emit errorOccurred(core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const QJsonArray array = replyData(reply)
                                                           .value(QStringLiteral("chats"))
                                                           .toArray();
                              QList<data::ChatSummary> summaries;
                              for (const QJsonValue &value : array)
                                  summaries.append(data::json::chatSummaryFromJson(value.toObject()));

                              m_chats = summaries;
                              emit chatsChanged();
                              persistChats();
                          });
}

void ChatService::openChat(qint64 chatId)
{
    ChatState &state = m_states[chatId];
    if (state.loaded || state.loading) {
        if (state.loaded)
            emit messagesChanged(chatId);
        return;
    }

    state.loading = true;

    // Сначала показываем кэш с диска, затем страницу с сервера.
    if (m_history)
        m_history->loadMessages(chatId, core::limits::kHistoryPageSize * 2);

    QJsonObject request;
    request.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    request.insert(QStringLiteral("limit"), core::limits::kHistoryPageSize);

    m_connection->request(QStringLiteral("chat.open"), request,
                          [this, chatId](const QJsonObject &reply) {
                              ChatState &current = m_states[chatId];
                              current.loading = false;

                              if (!replyOk(reply)) {
                                  emit errorOccurred(core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const QJsonObject data = replyData(reply);
                              QList<data::Message> loaded;
                              const QJsonArray array = data.value(QStringLiteral("messages")).toArray();
                              for (const QJsonValue &value : array)
                                  loaded.append(data::json::messageFromJson(value.toObject()));

                              // Неотправленные сообщения должны остаться в конце ленты.
                              QList<data::Message> pending;
                              for (const data::Message &message : qAsConst(current.messages)) {
                                  if (message.state == data::DeliveryState::Pending
                                      || message.state == data::DeliveryState::Failed) {
                                      pending.append(message);
                                  }
                              }

                              current.messages = loaded + pending;
                              current.loaded = true;
                              current.hasMore = data.value(QStringLiteral("has_more")).toBool();

                              emit messagesChanged(chatId);
                          });
}

void ChatService::loadOlder(qint64 chatId)
{
    ChatState &state = m_states[chatId];
    if (state.loading || !state.hasMore)
        return;

    qint64 oldestSeq = 0;
    for (const data::Message &message : qAsConst(state.messages)) {
        if (message.seq > 0) {
            oldestSeq = message.seq;
            break;
        }
    }
    if (oldestSeq <= 0)
        return;

    state.loading = true;

    QJsonObject request;
    request.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    request.insert(QStringLiteral("before_seq"), static_cast<double>(oldestSeq));
    request.insert(QStringLiteral("limit"), core::limits::kHistoryPageSize);

    m_connection->request(QStringLiteral("chat.open"), request,
                          [this, chatId](const QJsonObject &reply) {
                              ChatState &current = m_states[chatId];
                              current.loading = false;

                              if (!replyOk(reply)) {
                                  emit errorOccurred(core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const QJsonObject data = replyData(reply);
                              QList<data::Message> older;
                              const QJsonArray array = data.value(QStringLiteral("messages")).toArray();
                              for (const QJsonValue &value : array)
                                  older.append(data::json::messageFromJson(value.toObject()));

                              current.hasMore = data.value(QStringLiteral("has_more")).toBool();
                              if (older.isEmpty())
                                  return;

                              current.messages = older + current.messages;
                              emit historyPrepend(chatId);
                              emit messagesChanged(chatId);
                          });
}

void ChatService::sendText(qint64 chatId, const QString &text)
{
    const QString body = text.trimmed();
    if (body.isEmpty())
        return;

    data::Message pending;
    pending.chatId = chatId;
    pending.senderId = m_currentUserId;
    pending.clientMsgId = core::newUuid();
    pending.kind = data::MessageKind::Text;
    pending.body = body;
    pending.createdAt = QDateTime::currentMSecsSinceEpoch();
    pending.state = data::DeliveryState::Pending;
    pending.senderDisplayName = m_currentUser.displayName;
    pending.senderHue = m_currentUser.avatarHue;

    ChatState &state = m_states[chatId];
    state.messages.append(pending);

    updateSummaryForMessage(chatId, pending);
    emit messagesChanged(chatId);
    persistChats();

    sendPending(pending);
}

void ChatService::sendPending(const data::Message &message)
{
    const qint64 chatId = message.chatId;
    const QString clientMsgId = message.clientMsgId;

    QJsonObject request;
    request.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    request.insert(QStringLiteral("client_msg_id"), clientMsgId);
    request.insert(QStringLiteral("kind"), data::messageKindToString(message.kind));
    request.insert(QStringLiteral("body"), message.body);

    m_connection->request(QStringLiteral("msg.send"), request,
                          [this, chatId, clientMsgId](const QJsonObject &reply) {
                              ChatState &current = m_states[chatId];

                              bool updated = false;
                              for (data::Message &stored : current.messages) {
                                  if (stored.clientMsgId != clientMsgId)
                                      continue;

                                  if (replyOk(reply)) {
                                      const QJsonObject data = replyData(reply);
                                      stored.id = static_cast<qint64>(
                                          data.value(QStringLiteral("message_id")).toDouble());
                                      stored.seq = static_cast<qint64>(
                                          data.value(QStringLiteral("seq")).toDouble());
                                      stored.createdAt = static_cast<qint64>(
                                          data.value(QStringLiteral("created_at")).toDouble());
                                      stored.state = data::DeliveryState::Sent;
                                      updated = true;
                                  } else if (core::proto::errorCodeOf(reply)
                                             == core::ErrorCode::Offline) {
                                      // Останется в очереди до восстановления соединения.
                                      stored.state = data::DeliveryState::Pending;
                                  } else {
                                      stored.state = data::DeliveryState::Failed;
                                      emit errorOccurred(core::proto::errorMessageOf(reply));
                                  }
                                  break;
                              }

                              if (updated) {
                                  data::Message saved;
                                  for (const data::Message &stored : qAsConst(current.messages)) {
                                      if (stored.clientMsgId == clientMsgId) {
                                          saved = stored;
                                          break;
                                      }
                                  }

                                  if (m_history && saved.id > 0) {
                                      m_history->saveMessage(saved);
                                      m_history->removeOutbox(clientMsgId);
                                  }
                                  persistChats();
                              } else {
                                  if (core::proto::errorCodeOf(reply) == core::ErrorCode::Offline)
                                      persistOutbox(chatId, clientMsgId);
                              }

                              emit messagesChanged(chatId);
                          });
}

void ChatService::persistOutbox(qint64 chatId, const QString &clientMsgId)
{
    if (!m_history)
        return;

    const ChatState &state = m_states[chatId];
    for (const data::Message &message : state.messages) {
        if (message.clientMsgId != clientMsgId)
            continue;

        data::OutboxEntry entry;
        entry.clientMsgId = clientMsgId;
        entry.chatId = chatId;
        entry.kind = message.kind;
        entry.body = message.body;
        entry.createdAt = message.createdAt;
        entry.state = data::DeliveryState::Pending;
        m_history->addOutbox(entry);
        return;
    }
}

void ChatService::flushOutbox()
{
    if (m_history)
        m_history->loadOutbox();
}

void ChatService::persistChats()
{
    if (m_history)
        m_history->saveChats(m_chats);
}

void ChatService::retryFailed(qint64 chatId, const QString &clientMsgId)
{
    ChatState &state = m_states[chatId];

    for (int i = 0; i < state.messages.size(); ++i) {
        const data::Message message = state.messages.at(i);
        if (message.clientMsgId != clientMsgId || message.state != data::DeliveryState::Failed)
            continue;

        state.messages.removeAt(i);
        emit messagesChanged(chatId);
        sendText(chatId, message.body);
        return;
    }
}

void ChatService::markRead(qint64 chatId)
{
    qint64 maxSeq = 0;
    const ChatState &state = m_states.value(chatId);
    for (const data::Message &message : state.messages) {
        if (message.seq > maxSeq)
            maxSeq = message.seq;
    }

    const int index = indexOfChat(chatId);
    if (index >= 0 && m_chats[index].unread != 0) {
        m_chats[index].unread = 0;
        emit chatsChanged();
    }

    if (maxSeq <= 0)
        return;

    QJsonObject request;
    request.insert(QStringLiteral("chat_id"), static_cast<double>(chatId));
    request.insert(QStringLiteral("up_to_seq"), static_cast<double>(maxSeq));
    m_connection->request(QStringLiteral("chat.read"), request, nullptr, 10000);
}

void ChatService::createDirect(const QString &username)
{
    QJsonObject request;
    request.insert(QStringLiteral("username"), username);

    m_connection->request(QStringLiteral("chat.direct"), request,
                          [this](const QJsonObject &reply) {
                              if (!replyOk(reply)) {
                                  emit errorOccurred(core::proto::errorMessageOf(reply));
                                  return;
                              }

                              const data::ChatSummary summary = data::json::chatSummaryFromJson(
                                  replyData(reply).value(QStringLiteral("chat")).toObject());
                              upsertSummary(summary);
                              emit chatAdded(summary.chat.id);
                          });
}

void ChatService::onEvent(const QString &event, const QJsonObject &data)
{
    if (event == QLatin1String(core::proto::event::kMsg)) {
        const data::Message message =
            data::json::messageFromJson(data.value(QStringLiteral("message")).toObject());
        if (message.id > 0)
            handleNewMessage(message);
        return;
    }

    if (event == QLatin1String(core::proto::event::kReceipt)) {
        const qint64 chatId = static_cast<qint64>(data.value(QStringLiteral("chat_id")).toDouble());
        const qint64 messageId =
            static_cast<qint64>(data.value(QStringLiteral("message_id")).toDouble());
        const QString state = data.value(QStringLiteral("state")).toString();
        applyReceipt(chatId, messageId,
                     state == QLatin1String("read") ? data::DeliveryState::Read
                                                    : data::DeliveryState::Delivered);
        return;
    }

    if (event == QLatin1String(core::proto::event::kRead)) {
        const qint64 chatId = static_cast<qint64>(data.value(QStringLiteral("chat_id")).toDouble());
        const qint64 userId = static_cast<qint64>(data.value(QStringLiteral("user_id")).toDouble());
        const qint64 upToSeq =
            static_cast<qint64>(data.value(QStringLiteral("up_to_seq")).toDouble());
        if (userId != m_currentUserId)
            applyRead(chatId, upToSeq);
        return;
    }

    if (event == QLatin1String(core::proto::event::kChat)) {
        const data::ChatSummary summary = data::json::chatSummaryFromJson(
            data.value(QStringLiteral("chat")).toObject());
        upsertSummary(summary);
        emit chatAdded(summary.chat.id);
        return;
    }

    if (event == QLatin1String(core::proto::event::kTyping)) {
        const qint64 chatId = static_cast<qint64>(data.value(QStringLiteral("chat_id")).toDouble());
        const qint64 userId = static_cast<qint64>(data.value(QStringLiteral("user_id")).toDouble());
        emit typingChanged(chatId, userId, data.value(QStringLiteral("on")).toBool());
        return;
    }

    if (event == QLatin1String(core::proto::event::kPresence)) {
        const qint64 userId = static_cast<qint64>(data.value(QStringLiteral("user_id")).toDouble());
        emit presenceChanged(userId, data.value(QStringLiteral("online")).toBool());
    }
}

void ChatService::handleNewMessage(const data::Message &message)
{
    if (indexOfChat(message.chatId) < 0) {
        loadChats();
    } else {
        updateSummaryForMessage(message.chatId, message);
        persistChats();
    }

    if (m_history && message.id > 0)
        m_history->saveMessage(message);

    ChatState &state = m_states[message.chatId];
    if (state.loaded)
        insertMessageSorted(message.chatId, message);

    if (state.loaded)
        emit messagesChanged(message.chatId);
}

void ChatService::applyReceipt(qint64 chatId, qint64 messageId, data::DeliveryState state)
{
    ChatState &chatState = m_states[chatId];

    bool changed = false;
    for (data::Message &message : chatState.messages) {
        if (message.id != messageId || message.senderId != m_currentUserId)
            continue;
        if (static_cast<int>(state) > static_cast<int>(message.state)) {
            message.state = state;
            changed = true;
        }
    }

    if (changed)
        emit messagesChanged(chatId);
}

void ChatService::applyRead(qint64 chatId, qint64 upToSeq)
{
    ChatState &chatState = m_states[chatId];

    bool changed = false;
    for (data::Message &message : chatState.messages) {
        if (message.senderId == m_currentUserId && message.seq > 0 && message.seq <= upToSeq
            && message.state != data::DeliveryState::Read) {
            message.state = data::DeliveryState::Read;
            changed = true;
        }
    }

    if (changed)
        emit messagesChanged(chatId);
}

void ChatService::upsertSummary(const data::ChatSummary &summary)
{
    const int index = indexOfChat(summary.chat.id);
    if (index >= 0)
        m_chats[index] = summary;
    else
        m_chats.append(summary);

    emit chatsChanged();
}

void ChatService::insertMessageSorted(qint64 chatId, const data::Message &message)
{
    ChatState &state = m_states[chatId];

    for (const data::Message &existing : qAsConst(state.messages)) {
        if (existing.id == message.id && message.id > 0)
            return;
        if (!message.clientMsgId.isEmpty() && existing.clientMsgId == message.clientMsgId)
            return;
    }

    if (message.seq <= 0) {
        state.messages.append(message);
        return;
    }

    int insertAt = state.messages.size();
    for (int i = 0; i < state.messages.size(); ++i) {
        const data::Message &existing = state.messages.at(i);
        if (existing.seq <= 0 || existing.seq > message.seq) {
            insertAt = i;
            break;
        }
    }
    state.messages.insert(insertAt, message);
}

void ChatService::updateSummaryForMessage(qint64 chatId, const data::Message &message)
{
    const int index = indexOfChat(chatId);
    if (index < 0) {
        loadChats();
        return;
    }

    data::ChatSummary &summary = m_chats[index];
    summary.lastKind = message.kind;
    summary.lastBody = message.body;
    summary.lastAt = message.createdAt;
    summary.lastSeq = qMax(summary.lastSeq, message.seq);

    if (message.senderId != m_currentUserId && chatId != m_activeChatId)
        ++summary.unread;

    // Поднимаем чат в начало списка.
    if (index > 0)
        m_chats.move(index, 0);

    emit chatsChanged();
}

} // namespace app
