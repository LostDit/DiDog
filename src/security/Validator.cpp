#include "security/Validator.h"

#include "core/Limits.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace security {
namespace {

QString stripControlCharacters(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isPrint() || ch == QLatin1Char(' '))
            result.append(ch);
    }
    return result;
}

} // namespace

CheckResult checkUsername(const QString &username)
{
    const QString trimmed = username.trimmed();
    if (trimmed.size() < core::limits::kUserNameMinChars
        || trimmed.size() > core::limits::kUserNameMaxChars) {
        return CheckResult::failure(
            QStringLiteral("Логин должен содержать от %1 до %2 символов.")
                .arg(core::limits::kUserNameMinChars)
                .arg(core::limits::kUserNameMaxChars));
    }

    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
    if (!pattern.match(trimmed).hasMatch()) {
        return CheckResult::failure(
            QStringLiteral("Допустимы латинские буквы, цифры, «_», «-», «.»; "
                           "первый символ — буква или цифра."));
    }

    return CheckResult::success();
}

CheckResult checkDisplayName(const QString &displayName)
{
    const QString normalized = normalizeDisplayName(displayName);
    if (normalized.isEmpty())
        return CheckResult::failure(QStringLiteral("Введите отображаемое имя."));

    if (normalized.size() > core::limits::kDisplayNameMaxChars) {
        return CheckResult::failure(QStringLiteral("Имя не должно превышать %1 символов.")
                                        .arg(core::limits::kDisplayNameMaxChars));
    }
    return CheckResult::success();
}

CheckResult checkPassword(const QString &password)
{
    if (password.size() < core::limits::kPasswordMinChars) {
        return CheckResult::failure(QStringLiteral("Пароль должен содержать не менее %1 символов.")
                                        .arg(core::limits::kPasswordMinChars));
    }
    if (password.size() > core::limits::kPasswordMaxChars) {
        return CheckResult::failure(QStringLiteral("Пароль не должен превышать %1 символов.")
                                        .arg(core::limits::kPasswordMaxChars));
    }
    if (password.trimmed().isEmpty())
        return CheckResult::failure(QStringLiteral("Пароль не может состоять из пробелов."));

    return CheckResult::success();
}

CheckResult checkMessageText(const QString &text)
{
    if (text.isEmpty())
        return CheckResult::failure(QStringLiteral("Сообщение пустое."));

    if (text.size() > core::limits::kMessageMaxChars) {
        return CheckResult::failure(QStringLiteral("Сообщение длиннее %1 символов.")
                                        .arg(core::limits::kMessageMaxChars));
    }
    return CheckResult::success();
}

CheckResult checkAttachmentName(const QString &fileName)
{
    const QString safe = safeFileName(fileName);
    if (safe.isEmpty())
        return CheckResult::failure(QStringLiteral("Некорректное имя файла."));

    if (safe.size() > core::limits::kFileNameMaxChars) {
        return CheckResult::failure(QStringLiteral("Имя файла длиннее %1 символов.")
                                        .arg(core::limits::kFileNameMaxChars));
    }
    return CheckResult::success();
}

QString normalizeDisplayName(const QString &displayName)
{
    return stripControlCharacters(displayName).simplified();
}

QString safeFileName(const QString &fileName)
{
    QString base = QFileInfo(stripControlCharacters(fileName)).fileName();

    // Защита от «..» и Windows-специфичных хвостов вроде «C:file».
    base.remove(QLatin1Char(':'));
    while (base.startsWith(QLatin1Char('.')) && base.size() > 1 && base.at(1) == QLatin1Char('.'))
        base.remove(0, 1);

    if (base == QLatin1String(".") || base == QLatin1String(".."))
        base.clear();

    return base.trimmed();
}

bool isValidSha256(const QString &hex)
{
    if (hex.size() != 64)
        return false;

    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return pattern.match(hex).hasMatch();
}

} // namespace security
