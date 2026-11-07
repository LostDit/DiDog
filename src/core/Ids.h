#pragma once

#include <QByteArray>
#include <QString>

namespace core {

// UUID v4 в каноническом виде без фигурных скобок: "9f1c…".
QString newUuid();

// Криптостойкий случайный токен: bytes байт из QRandomGenerator::system(),
// закодированных в hex (длина строки = 2 * bytes).
QString newToken(int bytes = 32);

// Криптостойкие случайные байты.
QByteArray randomBytes(int count);

// hex-представление (нижний регистр) и обратное преобразование.
QString hexEncode(const QByteArray &bytes);
QByteArray hexDecode(const QString &hex, bool *ok = nullptr);

} // namespace core
