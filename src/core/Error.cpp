#include "core/Error.h"

#include <QCoreApplication>

namespace core {

QString errorCodeToString(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None:               return QStringLiteral("OK");
    case ErrorCode::BadFrame:           return QStringLiteral("BAD_FRAME");
    case ErrorCode::BadRequest:         return QStringLiteral("BAD_REQUEST");
    case ErrorCode::VersionUnsupported: return QStringLiteral("VERSION_UNSUPPORTED");
    case ErrorCode::AuthRequired:       return QStringLiteral("AUTH_REQUIRED");
    case ErrorCode::AuthFailed:         return QStringLiteral("AUTH_FAILED");
    case ErrorCode::UserExists:         return QStringLiteral("USER_EXISTS");
    case ErrorCode::NotMember:          return QStringLiteral("NOT_MEMBER");
    case ErrorCode::NotFound:           return QStringLiteral("NOT_FOUND");
    case ErrorCode::TooLarge:           return QStringLiteral("TOO_LARGE");
    case ErrorCode::RateLimited:        return QStringLiteral("RATE_LIMITED");
    case ErrorCode::Conflict:           return QStringLiteral("CONFLICT");
    case ErrorCode::Timeout:            return QStringLiteral("TIMEOUT");
    case ErrorCode::Internal:           return QStringLiteral("INTERNAL");
    }
    return QStringLiteral("INTERNAL");
}

ErrorCode errorCodeFromString(const QString &text)
{
    static const struct { const char *name; ErrorCode code; } table[] = {
        {"OK",                  ErrorCode::None},
        {"BAD_FRAME",           ErrorCode::BadFrame},
        {"BAD_REQUEST",         ErrorCode::BadRequest},
        {"VERSION_UNSUPPORTED", ErrorCode::VersionUnsupported},
        {"AUTH_REQUIRED",       ErrorCode::AuthRequired},
        {"AUTH_FAILED",         ErrorCode::AuthFailed},
        {"USER_EXISTS",         ErrorCode::UserExists},
        {"NOT_MEMBER",          ErrorCode::NotMember},
        {"NOT_FOUND",           ErrorCode::NotFound},
        {"TOO_LARGE",           ErrorCode::TooLarge},
        {"RATE_LIMITED",        ErrorCode::RateLimited},
        {"CONFLICT",            ErrorCode::Conflict},
        {"TIMEOUT",             ErrorCode::Timeout},
        {"INTERNAL",            ErrorCode::Internal},
    };

    for (const auto &entry : table) {
        if (text == QLatin1String(entry.name))
            return entry.code;
    }
    return ErrorCode::Internal;
}

QString errorCodeUserMessage(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None:
        return QString();
    case ErrorCode::BadFrame:
        return QCoreApplication::translate("core", "Повреждённый сетевой пакет.");
    case ErrorCode::BadRequest:
        return QCoreApplication::translate("core", "Некорректный запрос.");
    case ErrorCode::VersionUnsupported:
        return QCoreApplication::translate("core", "Версия протокола не поддерживается сервером.");
    case ErrorCode::AuthRequired:
        return QCoreApplication::translate("core", "Требуется вход в систему.");
    case ErrorCode::AuthFailed:
        return QCoreApplication::translate("core", "Неверный логин или пароль.");
    case ErrorCode::UserExists:
        return QCoreApplication::translate("core", "Этот логин уже занят.");
    case ErrorCode::NotMember:
        return QCoreApplication::translate("core", "Нет доступа к этому чату.");
    case ErrorCode::NotFound:
        return QCoreApplication::translate("core", "Запись не найдена.");
    case ErrorCode::TooLarge:
        return QCoreApplication::translate("core", "Превышен допустимый размер.");
    case ErrorCode::RateLimited:
        return QCoreApplication::translate("core", "Слишком много запросов, попробуйте позже.");
    case ErrorCode::Conflict:
        return QCoreApplication::translate("core", "Конфликт данных.");
    case ErrorCode::Timeout:
        return QCoreApplication::translate("core", "Сервер не ответил вовремя.");
    case ErrorCode::Internal:
        return QCoreApplication::translate("core", "Внутренняя ошибка сервера.");
    }
    return QCoreApplication::translate("core", "Неизвестная ошибка.");
}

} // namespace core
