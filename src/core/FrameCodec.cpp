#include "core/FrameCodec.h"

#include "core/Limits.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QtEndian>

namespace core {
namespace {

constexpr int kHeaderSize = 8;
constexpr int kChunkPrefixSize = 8;

bool isKnownType(quint8 raw)
{
    return raw == static_cast<quint8>(FrameType::Json)
        || raw == static_cast<quint8>(FrameType::Binary);
}

} // namespace

QByteArray FrameCodec::encode(FrameType type, const QByteArray &payload)
{
    QByteArray frame;
    frame.reserve(kHeaderSize + payload.size());

    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(payload.size())
           << static_cast<quint8>(type)
           << static_cast<quint8>(0)
           << static_cast<quint16>(0);

    if (!payload.isEmpty())
        frame.append(payload);
    return frame;
}

void FrameCodec::append(const QByteArray &chunk)
{
    if (!chunk.isEmpty())
        m_buffer.append(chunk);
}

FrameCodec::Status FrameCodec::next(Frame *frame, QString *error)
{
    if (m_buffer.size() < kHeaderSize)
        return Status::NeedMore;

    const auto *raw = reinterpret_cast<const uchar *>(m_buffer.constData());
    const quint32 payloadLength = qFromBigEndian<quint32>(raw);
    const quint8 typeRaw = raw[4];

    if (!isKnownType(typeRaw)) {
        if (error)
            *error = QStringLiteral("Неизвестный тип кадра: %1").arg(typeRaw);
        m_buffer.clear();
        return Status::Error;
    }

    const auto type = static_cast<FrameType>(typeRaw);

    // Лимит зависит от типа: JSON ограничен строже, чем чанк файла.
    const quint32 limit = (type == FrameType::Json)
                              ? limits::kJsonFrameMaxBytes
                              : limits::kChunkPayloadMaxBytes;
    if (payloadLength > limit) {
        if (error) {
            *error = QStringLiteral("Кадр превышает лимит: %1 > %2 байт")
                         .arg(payloadLength)
                         .arg(limit);
        }
        m_buffer.clear();
        return Status::Error;
    }

    if (static_cast<quint32>(m_buffer.size()) < kHeaderSize + payloadLength)
        return Status::NeedMore;

    const QByteArray payload = m_buffer.mid(kHeaderSize, static_cast<int>(payloadLength));
    m_buffer.remove(0, kHeaderSize + static_cast<int>(payloadLength));

    if (type == FrameType::Json) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) {
                *error = QStringLiteral("Некорректный JSON-кадр: %1")
                             .arg(parseError.errorString());
            }
            return Status::Error;
        }
    } else if (payload.size() < kChunkPrefixSize) {
        if (error)
            *error = QStringLiteral("Чанк короче заголовка передачи");
        return Status::Error;
    }

    if (frame) {
        frame->type = type;
        frame->payload = payload;
    }
    return Status::FrameReady;
}

QByteArray encodeChunk(quint32 transferId, quint32 sequence, const QByteArray &data)
{
    QByteArray payload;
    payload.reserve(kChunkPrefixSize + data.size());

    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << transferId << sequence;
    payload.append(data);
    return payload;
}

bool decodeChunk(const QByteArray &payload, quint32 *transferId, quint32 *sequence,
                 QByteArray *data)
{
    if (payload.size() < kChunkPrefixSize)
        return false;

    const auto *raw = reinterpret_cast<const uchar *>(payload.constData());
    if (transferId)
        *transferId = qFromBigEndian<quint32>(raw);
    if (sequence)
        *sequence = qFromBigEndian<quint32>(raw + 4);
    if (data)
        *data = payload.mid(kChunkPrefixSize);
    return true;
}

} // namespace core
