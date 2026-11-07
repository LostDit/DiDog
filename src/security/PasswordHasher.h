#pragma once

#include <QByteArray>
#include <QString>

namespace security {

struct PasswordHash {
    QByteArray salt;
    QByteArray hash;
    int iterations = 0;
};

// PBKDF2-HMAC-SHA256. Одна итерация SHA-256 перебирается на GPU слишком быстро,
// поэтому используется адаптивная функция с солью и большим числом итераций.
class PasswordHasher
{
public:
    static constexpr int kIterations = 120000;
    static constexpr int kSaltBytes = 16;
    static constexpr int kKeyBytes = 32;

    static PasswordHash create(const QString &password);

    static bool verify(const QString &password, const PasswordHash &stored);

    static QByteArray derive(const QString &password, const QByteArray &salt, int iterations);

    // Побайтовое сравнение без раннего выхода: время не зависит от места расхождения.
    static bool constantTimeEquals(const QByteArray &left, const QByteArray &right);
};

} // namespace security
