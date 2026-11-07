#pragma once

#include <QString>

namespace security {

struct CheckResult {
    bool ok = true;
    QString message;

    static CheckResult success() { return CheckResult{}; }
    static CheckResult failure(const QString &text)
    {
        CheckResult result;
        result.ok = false;
        result.message = text;
        return result;
    }
};

// Валидация выполняется и на клиенте (быстрая подсказка), и на сервере (источник истины).
CheckResult checkUsername(const QString &username);
CheckResult checkDisplayName(const QString &displayName);
CheckResult checkPassword(const QString &password);
CheckResult checkMessageText(const QString &text);
CheckResult checkAttachmentName(const QString &fileName);

// trim + схлопывание пробелов + удаление управляющих символов.
QString normalizeDisplayName(const QString &displayName);

// Только базовое имя: отсекает "../", "C:\\", любые разделители и управляющие символы.
QString safeFileName(const QString &fileName);

bool isValidSha256(const QString &hex);

} // namespace security
