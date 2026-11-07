#pragma once

#include "data/Models.h"

#include <QJsonObject>

namespace data::json {

QJsonObject userToJson(const User &user);
User userFromJson(const QJsonObject &object);

QJsonObject attachmentToJson(const Attachment &attachment);
Attachment attachmentFromJson(const QJsonObject &object);

QJsonObject messageToJson(const Message &message);
Message messageFromJson(const QJsonObject &object);

QJsonObject chatToJson(const Chat &chat);
Chat chatFromJson(const QJsonObject &object);

QJsonObject chatSummaryToJson(const ChatSummary &summary);
ChatSummary chatSummaryFromJson(const QJsonObject &object);

} // namespace data::json
