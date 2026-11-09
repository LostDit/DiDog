#include "server/LiveKitToken.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QtTest>

// JWT для LiveKit: структура, claims и подпись HS256.
class TestLiveKitToken : public QObject
{
    Q_OBJECT

private slots:
    void tokenHasValidClaimsAndSignature();
    void emptyConfigurationGivesEmptyToken();
};

void TestLiveKitToken::tokenHasValidClaimsAndSignature()
{
    const QString token = server::createLiveKitToken(
        QStringLiteral("api-key"), QStringLiteral("api-secret"),
        QStringLiteral("chat-42"), 7, QStringLiteral("LostDead"), 3600);
    QVERIFY(!token.isEmpty());

    const QStringList parts = token.split(QLatin1Char('.'));
    QCOMPARE(parts.size(), 3);

    const QByteArray header = QByteArray::fromBase64(
        parts.at(0).toLatin1(), QByteArray::Base64UrlEncoding);
    const QByteArray payload = QByteArray::fromBase64(
        parts.at(1).toLatin1(), QByteArray::Base64UrlEncoding);

    const QJsonObject headerJson = QJsonDocument::fromJson(header).object();
    QCOMPARE(headerJson.value(QStringLiteral("alg")).toString(), QStringLiteral("HS256"));

    const QJsonObject payloadJson = QJsonDocument::fromJson(payload).object();
    QCOMPARE(payloadJson.value(QStringLiteral("iss")).toString(), QStringLiteral("api-key"));
    QCOMPARE(payloadJson.value(QStringLiteral("sub")).toString(), QStringLiteral("7"));
    QCOMPARE(payloadJson.value(QStringLiteral("name")).toString(), QStringLiteral("LostDead"));
    QCOMPARE(payloadJson.value(QStringLiteral("video")).toObject()
                 .value(QStringLiteral("room")).toString(),
             QStringLiteral("chat-42"));
    QVERIFY(payloadJson.value(QStringLiteral("exp")).toDouble()
            > QDateTime::currentSecsSinceEpoch());

    const QByteArray expectedSignature = QMessageAuthenticationCode::hash(
        parts.at(0).toLatin1() + '.' + parts.at(1).toLatin1(),
        QByteArrayLiteral("api-secret"), QCryptographicHash::Sha256);
    const QByteArray actualSignature = QByteArray::fromBase64(
        parts.at(2).toLatin1(), QByteArray::Base64UrlEncoding);
    QCOMPARE(actualSignature, expectedSignature);
}

void TestLiveKitToken::emptyConfigurationGivesEmptyToken()
{
    QVERIFY(server::createLiveKitToken(QString(), QString(), QStringLiteral("chat-1"), 1,
                                       QStringLiteral("x"))
                .isEmpty());
    QVERIFY(server::createLiveKitToken(QStringLiteral("k"), QStringLiteral("s"), QString(), 1,
                                       QStringLiteral("x"))
                .isEmpty());
}

QTEST_GUILESS_MAIN(TestLiveKitToken)
#include "tst_livekit_token.moc"
