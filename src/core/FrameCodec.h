#pragma once

#include <QByteArray>
#include <QString>

namespace core {

enum class FrameType : quint8 {
    Json = 1,   // JSON-конверт
    Binary = 2  // чанк файла: transfer_id + seq + данные
};

struct Frame {
    FrameType type = FrameType::Json;
    QByteArray payload;
};

// Сборка кадров из TCP-потока: частичные кадры, несколько кадров в одном сегменте,
// защита от превышения лимитов (docs/protocol.md §1).
class FrameCodec
{
public:
    enum class Status {
        NeedMore,
        FrameReady,
        Error
    };

    Status next(Frame *frame, QString *error);
    void append(const QByteArray &chunk);
    void clear() { m_buffer.clear(); }

    int bufferedBytes() const { return m_buffer.size(); }

    static QByteArray encode(FrameType type, const QByteArray &payload);

private:
    QByteArray m_buffer;
};

// Бинарный чанк: uint32 transferId | uint32 seq | data.
QByteArray encodeChunk(quint32 transferId, quint32 sequence, const QByteArray &data);
bool decodeChunk(const QByteArray &payload, quint32 *transferId, quint32 *sequence,
                 QByteArray *data);

} // namespace core
