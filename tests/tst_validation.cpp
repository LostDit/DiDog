#include "security/Validator.h"

#include <QtTest>

class TestValidator : public QObject
{
    Q_OBJECT

private slots:
    void usernameAcceptsValid_data();
    void usernameAcceptsValid();
    void usernameRejectsInvalid_data();
    void usernameRejectsInvalid();

    void displayNameIsNormalized();
    void passwordLengthIsEnforced();
    void messageLengthIsEnforced();

    void safeFileNameStripsTraversal();
    void sha256IsValidated();
};

void TestValidator::usernameAcceptsValid_data()
{
    QTest::addColumn<QString>("username");
    QTest::newRow("simple") << QStringLiteral("alice");
    QTest::newRow("digits") << QStringLiteral("user2026");
    QTest::newRow("dots") << QStringLiteral("a.b-c_d");
    QTest::newRow("max") << QString(32, QLatin1Char('a'));
}

void TestValidator::usernameAcceptsValid()
{
    QFETCH(QString, username);
    QVERIFY2(security::checkUsername(username).ok,
             qPrintable(security::checkUsername(username).message));
}

void TestValidator::usernameRejectsInvalid_data()
{
    QTest::addColumn<QString>("username");
    QTest::newRow("short") << QStringLiteral("ab");
    QTest::newRow("long") << QString(33, QLatin1Char('a'));
    QTest::newRow("cyrillic") << QStringLiteral("алиса");
    QTest::newRow("space") << QStringLiteral("al ice");
    QTest::newRow("starts-with-dot") << QStringLiteral(".alice");
    QTest::newRow("at-sign") << QStringLiteral("alice@host");
}

void TestValidator::usernameRejectsInvalid()
{
    QFETCH(QString, username);
    QVERIFY(!security::checkUsername(username).ok);
}

void TestValidator::displayNameIsNormalized()
{
    QCOMPARE(security::normalizeDisplayName(QStringLiteral("  Аня   Петрова  ")),
             QStringLiteral("Аня Петрова"));
    QCOMPARE(security::normalizeDisplayName(QStringLiteral("Ан\u0007я")),
             QStringLiteral("Аня"));
    QVERIFY(!security::checkDisplayName(QStringLiteral("   ")).ok);
    QVERIFY(security::checkDisplayName(QStringLiteral("Аня")).ok);
}

void TestValidator::passwordLengthIsEnforced()
{
    QVERIFY(!security::checkPassword(QStringLiteral("short")).ok);
    QVERIFY(security::checkPassword(QStringLiteral("longenough1")).ok);
    QVERIFY(!security::checkPassword(QString(129, QLatin1Char('x'))).ok);
}

void TestValidator::messageLengthIsEnforced()
{
    QVERIFY(!security::checkMessageText(QString()).ok);
    QVERIFY(security::checkMessageText(QStringLiteral("привет")).ok);
    QVERIFY(!security::checkMessageText(QString(4001, QLatin1Char('a'))).ok);
}

void TestValidator::safeFileNameStripsTraversal()
{
    QCOMPARE(security::safeFileName(QStringLiteral("../../etc/passwd")),
             QStringLiteral("passwd"));
    QCOMPARE(security::safeFileName(QStringLiteral("C:\\Windows\\system32\\cmd.exe")),
             QStringLiteral("cmd.exe"));
    QCOMPARE(security::safeFileName(QStringLiteral("photo.jpg")),
             QStringLiteral("photo.jpg"));
    QVERIFY(security::safeFileName(QStringLiteral("..")).isEmpty());
}

void TestValidator::sha256IsValidated()
{
    QVERIFY(security::isValidSha256(QString(64, QLatin1Char('a'))));
    QVERIFY(!security::isValidSha256(QString(63, QLatin1Char('a'))));
    QVERIFY(!security::isValidSha256(QString(64, QLatin1Char('z'))));
}

QTEST_GUILESS_MAIN(TestValidator)
#include "tst_validation.moc"
