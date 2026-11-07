#include "security/PasswordHasher.h"

#include <QElapsedTimer>
#include <QtTest>

class TestPasswordHasher : public QObject
{
    Q_OBJECT

private slots:
    void createProducesSaltAndHash();
    void verifyAcceptsCorrectPassword();
    void verifyRejectsWrongPassword();
    void saltsAreUnique();
    void deriveIsDeterministic();
    void verifyRejectsEmptyStoredHash();
    void constantTimeEqualsMatchesBytes();

    void createIsSlow_data();
    void createIsSlow();
};

void TestPasswordHasher::createProducesSaltAndHash()
{
    const security::PasswordHash hash = security::PasswordHasher::create(QStringLiteral("secret123"));

    QCOMPARE(hash.salt.size(), security::PasswordHasher::kSaltBytes);
    QCOMPARE(hash.hash.size(), security::PasswordHasher::kKeyBytes);
    QCOMPARE(hash.iterations, security::PasswordHasher::kIterations);
}

void TestPasswordHasher::verifyAcceptsCorrectPassword()
{
    const security::PasswordHash hash = security::PasswordHasher::create(QStringLiteral("Пароль-123"));
    QVERIFY(security::PasswordHasher::verify(QStringLiteral("Пароль-123"), hash));
}

void TestPasswordHasher::verifyRejectsWrongPassword()
{
    const security::PasswordHash hash = security::PasswordHasher::create(QStringLiteral("secret123"));
    QVERIFY(!security::PasswordHasher::verify(QStringLiteral("secret124"), hash));
    QVERIFY(!security::PasswordHasher::verify(QString(), hash));
}

void TestPasswordHasher::saltsAreUnique()
{
    const auto first = security::PasswordHasher::create(QStringLiteral("same-password"));
    const auto second = security::PasswordHasher::create(QStringLiteral("same-password"));

    QVERIFY(first.salt != second.salt);
    QVERIFY(first.hash != second.hash);
}

void TestPasswordHasher::deriveIsDeterministic()
{
    const QByteArray salt = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QByteArray a = security::PasswordHasher::derive(QStringLiteral("pass"), salt, 1000);
    const QByteArray b = security::PasswordHasher::derive(QStringLiteral("pass"), salt, 1000);

    QCOMPARE(a, b);
    QCOMPARE(a.size(), security::PasswordHasher::kKeyBytes);
}

void TestPasswordHasher::verifyRejectsEmptyStoredHash()
{
    security::PasswordHash empty;
    QVERIFY(!security::PasswordHasher::verify(QStringLiteral("anything"), empty));
}

void TestPasswordHasher::constantTimeEqualsMatchesBytes()
{
    const QByteArray value = QByteArray::fromHex("deadbeef");
    QVERIFY(security::PasswordHasher::constantTimeEquals(value, value));
    QVERIFY(!security::PasswordHasher::constantTimeEquals(value, QByteArray::fromHex("deadbeee")));
    QVERIFY(!security::PasswordHasher::constantTimeEquals(value, QByteArray::fromHex("dead")));
    QVERIFY(security::PasswordHasher::constantTimeEquals(QByteArray(), QByteArray()));
}

void TestPasswordHasher::createIsSlow_data()
{
    QTest::addColumn<int>("iterations");
    QTest::newRow("120k") << security::PasswordHasher::kIterations;
}

// Защита от случайного «оптимизированного» до нуля числа итераций.
void TestPasswordHasher::createIsSlow()
{
    QFETCH(int, iterations);
    QVERIFY(iterations >= 100000);

    QElapsedTimer timer;
    timer.start();
    security::PasswordHasher::create(QStringLiteral("timing-password"));
    QVERIFY2(timer.elapsed() >= 10, "PBKDF2 выполнился подозрительно быстро");
}

QTEST_GUILESS_MAIN(TestPasswordHasher)
#include "tst_password.moc"
