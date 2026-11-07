#pragma once

#include <QString>

namespace core {

// Коды ошибок протокола. Строковое представление уходит в JSON, человекочитаемое —
// в интерфейс. См. docs/protocol.md §9.
enum class ErrorCode {
    None = 0,
    BadFrame,
    BadRequest,
    VersionUnsupported,
    AuthRequired,
    AuthFailed,
    UserExists,
    NotMember,
    NotFound,
    TooLarge,
    RateLimited,
    Conflict,
    Timeout,
    Offline, // клиентская ошибка: соединение отсутствует, запрос не отправлен
    Internal
};

QString errorCodeToString(ErrorCode code);

ErrorCode errorCodeFromString(const QString &text);

// Текст для пользователя на языке интерфейса (по умолчанию — русский).
QString errorCodeUserMessage(ErrorCode code);

} // namespace core
