#include "data/Database.h"
#include "data/Migrations.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QScopedPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

class TestDatabase : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createsFullSchema();
    void reportsCurrentVersion();
    void reopensExistingDatabase();
    void recoversFromCorruption();
    void rejectsNewerSchema();
    void enforcesForeignKeys();

private:
    QString databasePath() const { return m_dir->filePath(QStringLiteral("test.db")); }
    QStringList tables(QSqlDatabase &db) const;

    QScopedPointer<QTemporaryDir> m_dir;
};

void TestDatabase::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());
}

void TestDatabase::cleanup()
{
    m_dir.reset();
}

QStringList TestDatabase::tables(QSqlDatabase &db) const
{
    QStringList names;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'")))
        return names;

    while (query.next())
        names.append(query.value(0).toString());
    return names;
}

void TestDatabase::createsFullSchema()
{
    data::Database db(QStringLiteral("tst_schema"));
    QString error;
    QVERIFY2(db.open(databasePath(), &error), qPrintable(error));
    QVERIFY(db.isOpen());

    auto handle = db.handle();
    const QStringList names = tables(handle);

    const QStringList expected = {
        QStringLiteral("attachments"), QStringLiteral("chat_members"),
        QStringLiteral("chats"), QStringLiteral("messages"),
        QStringLiteral("meta"), QStringLiteral("outbox"),
        QStringLiteral("receipts"), QStringLiteral("sessions"),
        QStringLiteral("users")
    };

    for (const QString &table : expected)
        QVERIFY2(names.contains(table), qPrintable(QStringLiteral("нет таблицы ") + table));
}

void TestDatabase::reportsCurrentVersion()
{
    data::Database db(QStringLiteral("tst_version"));
    QString error;
    QVERIFY2(db.open(databasePath(), &error), qPrintable(error));

    QCOMPARE(db.schemaVersion(), data::migrations::kCurrentVersion);
}

void TestDatabase::reopensExistingDatabase()
{
    QString error;
    {
        data::Database db(QStringLiteral("tst_reopen_first"));
        QVERIFY2(db.open(databasePath(), &error), qPrintable(error));

        QSqlQuery insert(db.handle());
        insert.prepare(QStringLiteral(
            "INSERT INTO users(username, display_name, avatar_hue, created_at) "
            "VALUES(?, ?, ?, ?)"));
        insert.addBindValue(QStringLiteral("alice"));
        insert.addBindValue(QStringLiteral("Алиса"));
        insert.addBindValue(210);
        insert.addBindValue(QDateTime::currentMSecsSinceEpoch());
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    }

    data::Database reopened(QStringLiteral("tst_reopen_second"));
    QVERIFY2(reopened.open(databasePath(), &error), qPrintable(error));

    QSqlQuery select(reopened.handle());
    QVERIFY(select.exec(QStringLiteral("SELECT display_name FROM users WHERE username='alice'")));
    QVERIFY(select.next());
    QCOMPARE(select.value(0).toString(), QStringLiteral("Алиса"));
}

void TestDatabase::recoversFromCorruption()
{
    // Пишем мусор вместо заголовка SQLite — файл гарантированно не БД.
    {
        QFile file(databasePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4096, '\x7F'));
        file.close();
    }

    data::Database db(QStringLiteral("tst_corrupt"));
    QString error;
    QVERIFY2(db.open(databasePath(), &error), qPrintable(error));
    QCOMPARE(db.schemaVersion(), data::migrations::kCurrentVersion);

    const QStringList quarantined = QDir(m_dir->path())
                                        .entryList({QStringLiteral("*.corrupt-*")}, QDir::Files);
    QCOMPARE(quarantined.size(), 1);
}

void TestDatabase::rejectsNewerSchema()
{
    QString error;
    {
        data::Database db(QStringLiteral("tst_newer_first"));
        QVERIFY2(db.open(databasePath(), &error), qPrintable(error));

        QSqlQuery query(db.handle());
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 99")));
    }

    data::Database newer(QStringLiteral("tst_newer_second"));
    QVERIFY(!newer.open(databasePath(), &error));
    QVERIFY2(error.contains(QStringLiteral("более новой")), qPrintable(error));
}

void TestDatabase::enforcesForeignKeys()
{
    data::Database db(QStringLiteral("tst_fk"));
    QString error;
    QVERIFY2(db.open(databasePath(), &error), qPrintable(error));

    QSqlQuery query(db.handle());
    // chat_id = 999 не существует → внешний ключ должен отклонить вставку.
    const bool ok = query.exec(QStringLiteral(
        "INSERT INTO chat_members(chat_id, user_id, joined_at) VALUES(999, 999, 1)"));
    QVERIFY(!ok);
}

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_db.moc"
