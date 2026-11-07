#include "security/PasswordHasher.h"

#include "core/Ids.h"

#include <QCryptographicHash>
// В Qt 5.14 QPasswordDigestor живёт в модуле Network (в более новых ветках — в Core),
// поэтому подключаем по полному пути, который не зависит от версии раскладки.
#include <QtNetwork/qpassworddigestor.h>

namespace security {

PasswordHash PasswordHasher::create(const QString &password)
{
    PasswordHash result;
    result.salt = core::randomBytes(kSaltBytes);
    result.iterations = kIterations;
    result.hash = derive(password, result.salt, result.iterations);
    return result;
}

bool PasswordHasher::verify(const QString &password, const PasswordHash &stored)
{
    if (stored.salt.isEmpty() || stored.hash.isEmpty() || stored.iterations <= 0)
        return false;

    const QByteArray candidate = derive(password, stored.salt, stored.iterations);
    return constantTimeEquals(candidate, stored.hash);
}

QByteArray PasswordHasher::derive(const QString &password, const QByteArray &salt, int iterations)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                              password.toUtf8(),
                                              salt,
                                              iterations,
                                              kKeyBytes);
}

bool PasswordHasher::constantTimeEquals(const QByteArray &left, const QByteArray &right)
{
    if (left.size() != right.size())
        return false;

    unsigned char difference = 0;
    for (int i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left.at(i) ^ right.at(i));

    return difference == 0;
}

} // namespace security
