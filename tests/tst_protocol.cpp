#include "core/FrameCodec.h"
#include "core/Limits.h"
#include "core/Protocol.h"

#include <QDataStream>
#include <QtTest>

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void jsonRoundTrip();
    void partialFrameNeedsMore();
    void twoFramesInOneChunk();
    void binaryFrameKeepsPayload();
    void rejectsUnknownFrameType();
    void rejectsOversizedJsonFrame();
    void rejectsMalformedJson();
    void chunkRoundTrip();
    void requestHasEnvelope();
    void errorReplyCarriesCode();
    void eventHasType();

private:
    static QByteArray rawHeader(quint32 length, quint8 type);
};

QByteArray TestProtocol::rawHeader(quint32 length, quint8 type)
{
    QByteArray header;
    QDataStream stream(&header, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << length << type << quint8(0) << quint16(0);
    return header;
}

void TestProtocol::jsonRoundTrip()
{
    const QJsonObject object = core::proto::request(QStringLiteral("ping"), QJsonObject());
    const QByteArray payload = core::proto::encode(object);

    QJsonObject parsed;
    QString error;
    QVERIFY2(core::proto::decode(payload, &parsed, &error), qPrintable(error));
    QCOMPARE(parsed.value(QStringLiteral("type")).toString(), QStringLiteral("ping"));
    QCOMPARE(parsed.value(QStringLiteral("v")).toInt(), core::proto::kVersion);
    QVERIFY(!parsed.value(QStringLiteral("id")).toString().isEmpty());
}

void TestProtocol::partialFrameNeedsMore()
{
    const QByteArray payload = core::proto::encode(
        core::proto::request(QStringLiteral("chat.list"), QJsonObject()));
    const QByteArray frame = core::FrameCodec::encode(core::FrameType::Json, payload);

    core::FrameCodec codec;
    QString error;
    core::Frame result;

    codec.append(frame.left(frame.size() / 2));
    QCOMPARE(codec.next(&result, &error), core::FrameCodec::Status::NeedMore);

    codec.append(frame.mid(frame.size() / 2));
    QCOMPARE(codec.next(&result, &error), core::FrameCodec::Status::FrameReady);
    QCOMPARE(result.payload, payload);
}

void TestProtocol::twoFramesInOneChunk()
{
    const QByteArray first = core::FrameCodec::encode(
        core::FrameType::Json,
        core::proto::encode(core::proto::request(QStringLiteral("ping"), QJsonObject())));
    const QByteArray second = core::FrameCodec::encode(
        core::FrameType::Json,
        core::proto::encode(core::proto::request(QStringLiteral("chat.list"), QJsonObject())));

    core::FrameCodec codec;
    codec.append(first + second);

    core::Frame frame;
    QString error;
    QCOMPARE(codec.next(&frame, &error), core::FrameCodec::Status::FrameReady);
    QCOMPARE(codec.next(&frame, &error), core::FrameCodec::Status::FrameReady);
    QCOMPARE(codec.next(&frame, &error), core::FrameCodec::Status::NeedMore);
}

void TestProtocol::binaryFrameKeepsPayload()
{
    const QByteArray chunk = core::encodeChunk(7, 3, QByteArrayLiteral("payload"));
    const QByteArray frame = core::FrameCodec::encode(core::FrameType::Binary, chunk);

    core::FrameCodec codec;
    codec.append(frame);

    core::Frame result;
    QString error;
    QCOMPARE(codec.next(&result, &error), core::FrameCodec::Status::FrameReady);
    QCOMPARE(result.type, core::FrameType::Binary);

    quint32 transferId = 0;
    quint32 sequence = 0;
    QByteArray data;
    QVERIFY(core::decodeChunk(result.payload, &transferId, &sequence, &data));
    QCOMPARE(transferId, quint32(7));
    QCOMPARE(sequence, quint32(3));
    QCOMPARE(data, QByteArrayLiteral("payload"));
}

void TestProtocol::rejectsUnknownFrameType()
{
    core::FrameCodec codec;
    codec.append(rawHeader(0, 99));

    core::Frame frame;
    QString error;
    QCOMPARE(codec.next(&frame, &error), core::FrameCodec::Status::Error);
    QVERIFY(!error.isEmpty());
}

void TestProtocol::rejectsOversizedJsonFrame()
{
    core::FrameCodec codec;
    codec.append(rawHeader(core::limits::kJsonFrameMaxBytes + 1, 1));

    core::Frame frame;
    QString error;
    QCOMPARE(codec.next(&frame, &error), core::FrameCodec::Status::Error);
    QVERIFY(error.contains(QStringLiteral("лимит")));
}

void TestProtocol::rejectsMalformedJson()
{
    core::FrameCodec codec;
    codec.append(core::FrameCodec::encode(core::FrameType::Json, QByteArrayLiteral("not-json")));

    core::Frame frame;
    QString error;
    QCOMPARE(codec.next(&frame, &error), core::FrameCodec::Status::Error);
}

void TestProtocol::chunkRoundTrip()
{
    const QByteArray data(1000, 'x');
    const QByteArray payload = core::encodeChunk(1, 0, data);

    quint32 transferId = 0;
    quint32 sequence = 42;
    QByteArray parsed;
    QVERIFY(core::decodeChunk(payload, &transferId, &sequence, &parsed));
    QCOMPARE(transferId, quint32(1));
    QCOMPARE(sequence, quint32(0));
    QCOMPARE(parsed, data);

    QVERIFY(!core::decodeChunk(QByteArrayLiteral("short"), nullptr, nullptr, nullptr));
}

void TestProtocol::requestHasEnvelope()
{
    QJsonObject data;
    data.insert(QStringLiteral("chat_id"), 5);

    const QJsonObject object = core::proto::request(QStringLiteral("chat.open"), data);
    QCOMPARE(object.value(QStringLiteral("v")).toInt(), 1);
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("chat.open"));
    QCOMPARE(object.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("chat_id")).toInt(), 5);
}

void TestProtocol::errorReplyCarriesCode()
{
    const QJsonObject reply = core::proto::errorReply(
        QStringLiteral("req-1"), core::ErrorCode::AuthFailed);

    QCOMPARE(reply.value(QStringLiteral("ok")).toBool(true), false);
    QCOMPARE(reply.value(QStringLiteral("reply_to")).toString(), QStringLiteral("req-1"));
    QCOMPARE(core::proto::errorCodeOf(reply), core::ErrorCode::AuthFailed);
    QVERIFY(!core::proto::errorMessageOf(reply).isEmpty());

    const QJsonObject ok = core::proto::successReply(QStringLiteral("req-2"));
    QCOMPARE(core::proto::errorCodeOf(ok), core::ErrorCode::None);
}

void TestProtocol::eventHasType()
{
    const QJsonObject event = core::proto::eventObject(
        QStringLiteral("ev.msg"), QJsonObject());
    QCOMPARE(event.value(QStringLiteral("type")).toString(), QStringLiteral("ev"));
    QCOMPARE(event.value(QStringLiteral("event")).toString(), QStringLiteral("ev.msg"));
}

QTEST_GUILESS_MAIN(TestProtocol)
#include "tst_protocol.moc"
