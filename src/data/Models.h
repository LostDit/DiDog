#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace data {

// Тип сообщения. Строковое представление — часть протокола (docs/protocol.md).
enum class MessageKind {
    Text = 0,
    Image,
    Video,
    Gif,
    File
};

QString messageKindToString(MessageKind kind);
MessageKind messageKindFromString(const QString &text, bool *ok = nullptr);

// Состояние своего сообщения: локальная очередь → подтверждение сервера.
enum class DeliveryState {
    Pending = 0, // лежит в outbox, ещё не отправлено
    Sent,        // сервер сохранил (msg.ack)
    Delivered,   // клиент получателя подтвердил получение
    Read,        // получатель прочитал
    Failed       // не удалось отправить после повторов
};

QString deliveryStateToString(DeliveryState state);

struct User {
    qint64 id = 0;
    QString username;
    QString displayName;
    int avatarHue = 210;
    qint64 createdAt = 0;
};

struct Chat {
    qint64 id = 0;
    QString kind;      // "direct" | "group"
    QString title;     // для group; для direct пусто
    qint64 createdAt = 0;
};

struct Attachment {
    qint64 id = 0;
    qint64 messageId = 0;
    QString fileName;
    QString mime;
    qint64 byteSize = 0;
    QString sha256;
    QString relPath;
    QString thumbPath;
    int width = 0;
    int height = 0;
    int durationMs = 0;

    bool isValid() const { return id != 0; }
};

struct Message {
    qint64 id = 0;
    qint64 chatId = 0;
    qint64 senderId = 0;
    qint64 seq = 0;
    QString clientMsgId;
    MessageKind kind = MessageKind::Text;
    QString body;
    qint64 createdAt = 0;
    Attachment attachment;
    DeliveryState state = DeliveryState::Sent;

    // Локальная копия файла (у отправителя — исходник, у получателя — кэш)
    // и прогресс загрузки (-1, если не загружается).
    QString localPath;
    int uploadProgress = -1;

    // Скачивание вложения не удалось: UI показывает кнопку повтора.
    bool attachmentFailed = false;

    // Денормализация для UI: подтягивается JOIN'ом, чтобы лента не делала N+1 запросов.
    QString senderDisplayName;
    int senderHue = 210;

    bool hasAttachment() const { return attachment.isValid(); }
};

// Строка списка чатов: чат + собеседник + последнее сообщение + непрочитанные.
struct ChatSummary {
    Chat chat;
    qint64 peerId = 0;
    QString peerDisplayName;
    int peerHue = 210;
    QString lastBody;
    MessageKind lastKind = MessageKind::Text;
    qint64 lastAt = 0;
    qint64 lastSeq = 0;
    int unread = 0;
};

// Запись офлайн-очереди (только у клиента).
struct OutboxEntry {
    QString clientMsgId;
    qint64 chatId = 0;
    MessageKind kind = MessageKind::Text;
    QString body;
    QString attachPath;
    DeliveryState state = DeliveryState::Pending;
    int attempts = 0;
    qint64 createdAt = 0;
};

} // namespace data

Q_DECLARE_METATYPE(data::ChatSummary)
Q_DECLARE_METATYPE(data::Message)
Q_DECLARE_METATYPE(data::User)
Q_DECLARE_METATYPE(data::OutboxEntry)
Q_DECLARE_METATYPE(QList<data::ChatSummary>)
Q_DECLARE_METATYPE(QList<data::Message>)
Q_DECLARE_METATYPE(QList<data::OutboxEntry>)
