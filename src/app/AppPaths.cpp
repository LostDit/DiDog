#include "app/AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace app::paths {
namespace {

QString g_override;

QString resolveBaseDir()
{
    if (!g_override.isEmpty())
        return QDir::cleanPath(g_override);

    const QByteArray env = qgetenv("MESSENGER_DATA_DIR");
    if (!env.isEmpty())
        return QDir::cleanPath(QString::fromLocal8Bit(env));

    const QString standard = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!standard.isEmpty())
        return QDir::cleanPath(standard);

    return QDir::homePath() + QStringLiteral("/.messengerapp");
}

QString childDir(const QString &name)
{
    return resolveBaseDir() + QLatin1Char('/') + name;
}

} // namespace

void setOverrideDataDir(const QString &dir)
{
    g_override = dir.isEmpty() ? QString() : QDir::cleanPath(dir);
}

QString overrideDataDir()
{
    return g_override;
}

QString dataDir()
{
    return resolveBaseDir();
}

QString logsDir()
{
    return childDir(QStringLiteral("logs"));
}

QString attachmentsDir()
{
    return childDir(QStringLiteral("attachments"));
}

QString thumbnailsDir()
{
    return childDir(QStringLiteral("thumbnails"));
}

QString tmpDir()
{
    return childDir(QStringLiteral("tmp"));
}

QString settingsFile()
{
    return childDir(QStringLiteral("settings.ini"));
}

QString clientDbPath()
{
    return childDir(QStringLiteral("client.db"));
}

QString serverDbPath()
{
    return childDir(QStringLiteral("server.db"));
}

bool ensureDirectories(QString *error)
{
    const QStringList directories = {
        dataDir(), logsDir(), attachmentsDir(), thumbnailsDir(), tmpDir()
    };

    for (const QString &path : directories) {
        if (QDir().mkpath(path))
            continue;

        if (error)
            *error = QStringLiteral("Не удалось создать каталог: %1").arg(path);
        return false;
    }
    return true;
}

} // namespace app::paths
