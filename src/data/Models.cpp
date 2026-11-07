#include "data/Models.h"

namespace data {

QString messageKindToString(MessageKind kind)
{
    switch (kind) {
    case MessageKind::Text:  return QStringLiteral("text");
    case MessageKind::Image: return QStringLiteral("image");
    case MessageKind::Video: return QStringLiteral("video");
    case MessageKind::Gif:   return QStringLiteral("gif");
    case MessageKind::File:  return QStringLiteral("file");
    }
    return QStringLiteral("text");
}

MessageKind messageKindFromString(const QString &text, bool *ok)
{
    if (ok)
        *ok = true;

    if (text == QLatin1String("text"))
        return MessageKind::Text;
    if (text == QLatin1String("image"))
        return MessageKind::Image;
    if (text == QLatin1String("video"))
        return MessageKind::Video;
    if (text == QLatin1String("gif"))
        return MessageKind::Gif;
    if (text == QLatin1String("file"))
        return MessageKind::File;

    if (ok)
        *ok = false;
    return MessageKind::Text;
}

QString deliveryStateToString(DeliveryState state)
{
    switch (state) {
    case DeliveryState::Pending:   return QStringLiteral("pending");
    case DeliveryState::Sent:      return QStringLiteral("sent");
    case DeliveryState::Delivered: return QStringLiteral("delivered");
    case DeliveryState::Read:      return QStringLiteral("read");
    case DeliveryState::Failed:    return QStringLiteral("failed");
    }
    return QStringLiteral("sent");
}

} // namespace data
