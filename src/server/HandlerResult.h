#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QString>

namespace server {

// Результат обработки запроса: данные ответа либо код ошибки для клиента.
struct HandlerResult {
    core::ErrorCode code = core::ErrorCode::None;
    QString message;
    QJsonObject data;

    bool ok() const { return code == core::ErrorCode::None; }

    static HandlerResult success(const QJsonObject &payload = QJsonObject())
    {
        HandlerResult result;
        result.data = payload;
        return result;
    }

    static HandlerResult failure(core::ErrorCode code, const QString &text = QString())
    {
        HandlerResult result;
        result.code = code;
        result.message = text;
        return result;
    }
};

} // namespace server
