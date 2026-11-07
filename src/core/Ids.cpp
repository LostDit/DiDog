#include "core/Ids.h"

#include <QRandomGenerator>
#include <QUuid>

namespace core {

QString newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString newToken(int bytes)
{
    if (bytes <= 0)
        return QString();

    QByteArray buffer(bytes, Qt::Uninitialized);
    auto *generator = QRandomGenerator::system();
    for (int i = 0; i < bytes; i += 4) {
        const quint32 value = generator->generate();
        const int remaining = qMin(4, bytes - i);
        for (int b = 0; b < remaining; ++b)
            buffer[i + b] = static_cast<char>((value >> (8 * b)) & 0xFF);
    }
    return hexEncode(buffer);
}

QString hexEncode(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex());
}

QByteArray hexDecode(const QString &hex, bool *ok)
{
    const QByteArray decoded = QByteArray::fromHex(hex.toLatin1());
    if (ok)
        *ok = (decoded.size() * 2 == hex.size());
    return decoded;
}

} // namespace core
