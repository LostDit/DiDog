#include "data/JsonMapping.h"

#include <QJsonArray>
#include <QJsonValue>

namespace data::json {
namespace {

qint64 toInt64(const QJsonValue &value)
{
    return static_cast<qint64>(value.toDouble());
}

QJsonValue nullIfEmpty(const QString &text)
{
    return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
}

} // namespace

QJsonObject userToJson(const User &user)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), static_cast<double>(user.id));
    object.insert(QStringLiteral("username"), user.username);
    object.insert(QStringLiteral("display_name"), user.displayName);
    object.insert(QStringLiteral("avatar_hue"), user.avatarHue);
    return object;
}

User userFromJson(const QJsonObject &object)
{
    User user;
    user.id = toInt64(object.value(QStringLiteral("id")));
    user.username = object.value(QStringLiteral("username")).toString();
    user.displayName = object.value(QStringLiteral("display_name")).toString();
    user.avatarHue = object.value(QStringLiteral("avatar_hue")).toInt(210);
    return user;
}

QJsonObject attachmentToJson(const Attachment &attachment)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), static_cast<double>(attachment.id));
    object.insert(QStringLiteral("file_name"), attachment.fileName);
    object.insert(QStringLiteral("mime"), attachment.mime);
    object.insert(QStringLiteral("byte_size"), static_cast<double>(attachment.byteSize));
    object.insert(QStringLiteral("sha256"), attachment.sha256);
    object.insert(QStringLiteral("width"), attachment.width);
    object.insert(QStringLiteral("height"), attachment.height);
    object.insert(QStringLiteral("duration_ms"), attachment.durationMs);
    object.insert(QStringLiteral("thumb_path"), nullIfEmpty(attachment.thumbPath));
    return object;
}

Attachment attachmentFromJson(const QJsonObject &object)
{
    Attachment attachment;
    attachment.id = toInt64(object.value(QStringLiteral("id")));
    attachment.fileName = object.value(QStringLiteral("file_name")).toString();
    attachment.mime = object.value(QStringLiteral("mime")).toString();
    attachment.byteSize = toInt64(object.value(QStringLiteral("byte_size")));
    attachment.sha256 = object.value(QStringLiteral("sha256")).toString();
    attachment.thumbPath = object.value(QStringLiteral("thumb_path")).toString();
    attachment.width = object.value(QStringLiteral("width")).toInt();
    attachment.height = object.value(QStringLiteral("height")).toInt();
    attachment.durationMs = object.value(QStringLiteral("duration_ms")).toInt();
    return attachment;
}

QJsonObject messageToJson(const Message &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), static_cast<double>(message.id));
    object.insert(QStringLiteral("chat_id"), static_cast<double>(message.chatId));
    object.insert(QStringLiteral("seq"), static_cast<double>(message.seq));
    object.insert(QStringLiteral("client_msg_id"), message.clientMsgId);
    object.insert(QStringLiteral("kind"), messageKindToString(message.kind));
    object.insert(QStringLiteral("body"), message.body);
    object.insert(QStringLiteral("created_at"), static_cast<double>(message.createdAt));
    object.insert(QStringLiteral("deleted"), message.deleted);
    object.insert(QStringLiteral("edited_at"), static_cast<double>(message.editedAt));
    object.insert(QStringLiteral("reply_to_id"), static_cast<double>(message.replyToId));

    QJsonObject sender;
    sender.insert(QStringLiteral("id"), static_cast<double>(message.senderId));
    sender.insert(QStringLiteral("display_name"), message.senderDisplayName);
    sender.insert(QStringLiteral("avatar_hue"), message.senderHue);
    object.insert(QStringLiteral("sender"), sender);

    object.insert(QStringLiteral("attachment"),
                  message.hasAttachment() ? QJsonValue(attachmentToJson(message.attachment))
                                          : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("status"), deliveryStateToString(message.state));

    if (!message.reactionCounts.isEmpty()) {
        QJsonArray reactions;
        for (auto it = message.reactionCounts.constBegin();
             it != message.reactionCounts.constEnd(); ++it) {
            QJsonObject item;
            item.insert(QStringLiteral("emoji"), it.key());
            item.insert(QStringLiteral("count"), it.value());
            item.insert(QStringLiteral("me"), message.myReactions.contains(it.key()));
            reactions.append(item);
        }
        object.insert(QStringLiteral("reactions"), reactions);
    }
    return object;
}

Message messageFromJson(const QJsonObject &object)
{
    Message message;
    message.id = toInt64(object.value(QStringLiteral("id")));
    message.chatId = toInt64(object.value(QStringLiteral("chat_id")));
    message.seq = toInt64(object.value(QStringLiteral("seq")));
    message.clientMsgId = object.value(QStringLiteral("client_msg_id")).toString();
    message.kind = messageKindFromString(object.value(QStringLiteral("kind")).toString());
    message.body = object.value(QStringLiteral("body")).toString();
    message.createdAt = toInt64(object.value(QStringLiteral("created_at")));
    message.deleted = object.value(QStringLiteral("deleted")).toBool(false);
    message.editedAt = toInt64(object.value(QStringLiteral("edited_at")));
    message.replyToId = toInt64(object.value(QStringLiteral("reply_to_id")));

    const QJsonObject sender = object.value(QStringLiteral("sender")).toObject();
    message.senderId = toInt64(sender.value(QStringLiteral("id")));
    message.senderDisplayName = sender.value(QStringLiteral("display_name")).toString();
    message.senderHue = sender.value(QStringLiteral("avatar_hue")).toInt(210);

    const QJsonValue attachment = object.value(QStringLiteral("attachment"));
    if (attachment.isObject()) {
        message.attachment = attachmentFromJson(attachment.toObject());
        message.attachment.messageId = message.id;
    }

    const QJsonArray reactions = object.value(QStringLiteral("reactions")).toArray();
    for (const QJsonValue &value : reactions) {
        const QJsonObject item = value.toObject();
        const QString emoji = item.value(QStringLiteral("emoji")).toString();
        if (emoji.isEmpty())
            continue;
        message.reactionCounts.insert(emoji, item.value(QStringLiteral("count")).toInt());
        if (item.value(QStringLiteral("me")).toBool())
            message.myReactions.insert(emoji);
    }

    const QString status = object.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("pending"))
        message.state = DeliveryState::Pending;
    else if (status == QLatin1String("delivered"))
        message.state = DeliveryState::Delivered;
    else if (status == QLatin1String("read"))
        message.state = DeliveryState::Read;
    else if (status == QLatin1String("failed"))
        message.state = DeliveryState::Failed;
    else
        message.state = DeliveryState::Sent;

    return message;
}

QJsonObject chatToJson(const Chat &chat)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), static_cast<double>(chat.id));
    object.insert(QStringLiteral("kind"), chat.kind);
    object.insert(QStringLiteral("title"), nullIfEmpty(chat.title));
    object.insert(QStringLiteral("created_at"), static_cast<double>(chat.createdAt));
    return object;
}

Chat chatFromJson(const QJsonObject &object)
{
    Chat chat;
    chat.id = toInt64(object.value(QStringLiteral("id")));
    chat.kind = object.value(QStringLiteral("kind")).toString();
    chat.title = object.value(QStringLiteral("title")).toString();
    chat.createdAt = toInt64(object.value(QStringLiteral("created_at")));
    return chat;
}

QJsonObject chatSummaryToJson(const ChatSummary &summary)
{
    QJsonObject object = chatToJson(summary.chat);
    object.insert(QStringLiteral("unread_count"), summary.unread);
    object.insert(QStringLiteral("last_seq"), static_cast<double>(summary.lastSeq));
    object.insert(QStringLiteral("last_at"), static_cast<double>(summary.lastAt));

    QJsonObject peer;
    peer.insert(QStringLiteral("id"), static_cast<double>(summary.peerId));
    peer.insert(QStringLiteral("display_name"), summary.peerDisplayName);
    peer.insert(QStringLiteral("avatar_hue"), summary.peerHue);
    object.insert(QStringLiteral("peer"), peer);

    QJsonObject last;
    last.insert(QStringLiteral("kind"), messageKindToString(summary.lastKind));
    last.insert(QStringLiteral("body"), summary.lastBody);
    last.insert(QStringLiteral("created_at"), static_cast<double>(summary.lastAt));
    object.insert(QStringLiteral("last_message"), last);
    return object;
}

ChatSummary chatSummaryFromJson(const QJsonObject &object)
{
    ChatSummary summary;
    summary.chat = chatFromJson(object);
    summary.unread = object.value(QStringLiteral("unread_count")).toInt();
    summary.lastSeq = toInt64(object.value(QStringLiteral("last_seq")));
    summary.lastAt = toInt64(object.value(QStringLiteral("last_at")));

    const QJsonObject peer = object.value(QStringLiteral("peer")).toObject();
    summary.peerId = toInt64(peer.value(QStringLiteral("id")));
    summary.peerDisplayName = peer.value(QStringLiteral("display_name")).toString();
    summary.peerHue = peer.value(QStringLiteral("avatar_hue")).toInt(210);

    const QJsonObject last = object.value(QStringLiteral("last_message")).toObject();
    summary.lastKind = messageKindFromString(last.value(QStringLiteral("kind")).toString());
    summary.lastBody = last.value(QStringLiteral("body")).toString();
    if (summary.lastAt == 0)
        summary.lastAt = toInt64(last.value(QStringLiteral("created_at")));
    return summary;
}

QJsonObject chatMemberToJson(const ChatMember &member)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), static_cast<double>(member.userId));
    object.insert(QStringLiteral("username"), member.username);
    object.insert(QStringLiteral("display_name"), member.displayName);
    object.insert(QStringLiteral("avatar_hue"), member.avatarHue);
    object.insert(QStringLiteral("role"), member.role);
    object.insert(QStringLiteral("online"), member.online);
    return object;
}

ChatMember chatMemberFromJson(const QJsonObject &object)
{
    ChatMember member;
    member.userId = toInt64(object.value(QStringLiteral("id")));
    member.username = object.value(QStringLiteral("username")).toString();
    member.displayName = object.value(QStringLiteral("display_name")).toString();
    member.avatarHue = object.value(QStringLiteral("avatar_hue")).toInt(210);
    member.role = object.value(QStringLiteral("role")).toString(QStringLiteral("member"));
    member.online = object.value(QStringLiteral("online")).toBool(false);
    return member;
}

} // namespace data::json
