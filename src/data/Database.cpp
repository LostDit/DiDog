#include "data/Database.h"

#include "data/Migrations.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace data {
namespace {

bool execRaw(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (query.exec(sql))
        return true;

    if (error)
        *error = QStringLiteral("SQL не выполнен: %1 (%2)").arg(sql, query.lastError().text());
    return false;
}

// SQLite сообщает о повреждении разными формулировками — распознаём их,
// чтобы отличить «битый файл» от проблем с правами/блокировкой.
bool looksCorrupt(const QString &text)
{
    const QString lower = text.toLower();
    return lower.contains(QStringLiteral("not a database"))
        || lower.contains(QStringLiteral("malformed"))
        || lower.contains(QStringLiteral("database disk image"));
}

} // namespace

Database::Database(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

Database::~Database()
{
    close();
}

bool Database::open(const QString &filePath, QString *error)
{
    close();
    m_filePath = filePath;

    QString firstError;
    const OpenResult result = openInternal(filePath, &firstError);
    if (result == OpenResult::Ok)
        return true;

    if (result == OpenResult::Failed) {
        if (error)
            *error = firstError;
        return false;
    }

    // ── Восстановление: карантин повреждённого файла и чистая БД ───────────
    close();

    const QString quarantine = quarantinePath(filePath);
    QFile::rename(filePath, quarantine);
    QFile::remove(filePath + QStringLiteral("-wal"));
    QFile::remove(filePath + QStringLiteral("-shm"));

    qWarning().noquote() << QStringLiteral("База данных повреждена. Копия сохранена: %1")
                                .arg(quarantine);

    QString retryError;
    if (openInternal(filePath, &retryError) != OpenResult::Ok) {
        if (error) {
            *error = QStringLiteral("База повреждена (%1), восстановить не удалось: %2")
                         .arg(quarantine, retryError);
        }
        return false;
    }

    if (error)
        error->clear();
    return true;
}

void Database::close()
{
    if (!m_open && !QSqlDatabase::contains(m_connectionName))
        return;

    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid() && db.isOpen())
            db.close();
    }

    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);

    m_open = false;
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

int Database::schemaVersion() const
{
    if (!m_open)
        return -1;

    QSqlDatabase db = handle();
    return migrations::version(db);
}

QString Database::quarantinePath(const QString &filePath)
{
    const QString stamp = QDateTime::currentDateTime()
                              .toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return filePath + QStringLiteral(".corrupt-") + stamp;
}

Database::OpenResult Database::openInternal(const QString &filePath, QString *error)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(filePath);

    if (!db.open()) {
        if (error)
            *error = QStringLiteral("Не удалось открыть базу %1: %2")
                         .arg(filePath, db.lastError().text());
        QSqlDatabase::removeDatabase(m_connectionName);
        return OpenResult::Failed;
    }

    if (!configure(error)) {
        const bool corrupt = error && looksCorrupt(*error);
        db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        return corrupt ? OpenResult::Corrupt : OpenResult::Failed;
    }

    const OpenResult integrity = verifyIntegrity(error);
    if (integrity != OpenResult::Ok) {
        db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        return integrity;
    }

    if (!migrations::apply(db, error)) {
        const bool corrupt = error && looksCorrupt(*error);
        db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        return corrupt ? OpenResult::Corrupt : OpenResult::Failed;
    }

    m_open = true;
    return OpenResult::Ok;
}

bool Database::configure(QString *error)
{
    QSqlDatabase db = handle();

    const QStringList pragmas = {
        QStringLiteral("PRAGMA journal_mode = WAL"),
        QStringLiteral("PRAGMA synchronous = NORMAL"),
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("PRAGMA busy_timeout = 5000"),
    };

    for (const QString &sql : pragmas) {
        if (!execRaw(db, sql, error))
            return false;
    }
    return true;
}

Database::OpenResult Database::verifyIntegrity(QString *error)
{
    QSqlDatabase db = handle();

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        if (error)
            *error = QStringLiteral("Проверка целостности не выполнена: %1")
                         .arg(query.lastError().text());
        return OpenResult::Failed;
    }

    if (!query.next())
        return OpenResult::Ok;

    const QString result = query.value(0).toString();
    if (result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0)
        return OpenResult::Ok;

    if (error)
        *error = QStringLiteral("База данных повреждена: %1").arg(result);
    return OpenResult::Corrupt;
}

} // namespace data
