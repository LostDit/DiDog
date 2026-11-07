#include "app/SingleInstance.h"

namespace app {

SingleInstance::SingleInstance(const QString &lockFilePath)
    : m_lock(lockFilePath)
    , m_path(lockFilePath)
{
    // 30 секунд — стандартное время устаревания: замок убитого процесса будет снят.
    m_lock.setStaleLockTime(30 * 1000);
}

SingleInstance::~SingleInstance()
{
    release();
}

bool SingleInstance::tryAcquire()
{
    if (m_locked)
        return true;

    if (m_lock.tryLock(0)) {
        m_locked = true;
        m_error.clear();
        return true;
    }

    m_error = QStringLiteral("Приложение уже запущено (замок: %1).").arg(m_path);
    return false;
}

void SingleInstance::release()
{
    if (!m_locked)
        return;

    m_lock.unlock();
    m_locked = false;
}

} // namespace app
