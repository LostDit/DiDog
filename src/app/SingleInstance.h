#pragma once

#include <QLockFile>
#include <QString>

namespace app {

// Защита от второго экземпляра на том же каталоге данных.
class SingleInstance
{
public:
    explicit SingleInstance(const QString &lockFilePath);
    ~SingleInstance();

    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;

    bool tryAcquire();
    void release();

    bool isLocked() const { return m_locked; }
    QString errorString() const { return m_error; }
    QString lockPath() const { return m_path; }

private:
    QLockFile m_lock;
    QString m_path;
    bool m_locked = false;
    QString m_error;
};

} // namespace app
