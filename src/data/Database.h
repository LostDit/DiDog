#pragma once

#include <QSqlDatabase>
#include <QString>

namespace data {

// Владелец соединения SQLite. Один экземпляр — одно соединение, привязанное к потоку,
// в котором он создан (QSqlDatabase нельзя переносить между потоками).
class Database
{
public:
    explicit Database(const QString &connectionName);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Открывает файл, включает WAL/foreign_keys, проверяет целостность и применяет миграции.
    // Повреждённая БД уводится в карантин (<file>.corrupt-<ts>) и создаётся заново.
    bool open(const QString &filePath, QString *error);

    void close();
    bool isOpen() const { return m_open; }

    QSqlDatabase handle() const;
    QString filePath() const { return m_filePath; }
    int schemaVersion() const;

    // Имя файла карантина для указанного пути.
    static QString quarantinePath(const QString &filePath);

private:
    enum class OpenResult {
        Ok,
        Corrupt, // проверка целостности не прошла — файл нужно увести в карантин
        Failed
    };

    OpenResult openInternal(const QString &filePath, QString *error);
    bool configure(QString *error);
    OpenResult verifyIntegrity(QString *error);

    QString m_connectionName;
    QString m_filePath;
    bool m_open = false;
};

} // namespace data
