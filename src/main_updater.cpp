#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QDebug>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

bool waitForProcess(qint64 pid, int timeoutMs)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return true; // процесс уже завершился

    const DWORD result = WaitForSingleObject(process, static_cast<DWORD>(timeoutMs));
    CloseHandle(process);
    return result == WAIT_OBJECT_0 || result == WAIT_FAILED;
#else
    Q_UNUSED(pid)
    QThread::msleep(static_cast<unsigned long>(timeoutMs));
    return true;
#endif
}

bool copyFile(const QString &source, const QString &target, QString *error)
{
    QDir().mkpath(QFileInfo(target).absolutePath());

    if (QFileInfo::exists(target) && !QFile::remove(target)) {
        if (error)
            *error = QStringLiteral("Не удалось удалить %1").arg(target);
        return false;
    }

    if (!QFile::copy(source, target)) {
        if (error)
            *error = QStringLiteral("Не удалось скопировать %1").arg(source);
        return false;
    }
    return true;
}

QStringList collectFiles(const QString &root)
{
    QStringList files;
    QDirIterator iterator(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext())
        files.append(iterator.next());
    return files;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MessengerUpdater"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption sourceOption(QStringLiteral("source"), QStringLiteral("Каталог с новыми файлами"), QStringLiteral("dir"));
    QCommandLineOption targetOption(QStringLiteral("target"), QStringLiteral("Каталог установки"), QStringLiteral("dir"));
    QCommandLineOption restartOption(QStringLiteral("restart"), QStringLiteral("Что запустить после обновления"), QStringLiteral("exe"));
    QCommandLineOption pidOption(QStringLiteral("pid"), QStringLiteral("PID процесса, который нужно дождаться"), QStringLiteral("pid"), QStringLiteral("0"));
    parser.addOptions({sourceOption, targetOption, restartOption, pidOption});
    parser.process(app);

    const QString source = parser.value(sourceOption);
    const QString target = parser.value(targetOption);
    const QString restart = parser.value(restartOption);
    const qint64 pid = parser.value(pidOption).toLongLong();

    if (source.isEmpty() || target.isEmpty()) {
        qCritical().noquote() << "Updater: не заданы --source и --target";
        return 1;
    }

    if (pid > 0 && !waitForProcess(pid, 60000)) {
        qCritical().noquote() << "Updater: приложение не завершилось";
        return 1;
    }

    QThread::msleep(300);

    const QString backupRoot = source + QStringLiteral("/backup");
    const QDir targetDir(target);
    const QDir sourceDir(source);

    QStringList updated;
    QString error;

    for (const QString &path : collectFiles(source)) {
        const QString relative = sourceDir.relativeFilePath(path);

        if (relative == QLatin1String("apply.json") || relative.startsWith(QLatin1String("backup/")))
            continue;

        const QString destination = targetDir.absoluteFilePath(relative);

        if (QFileInfo::exists(destination)) {
            const QString backupPath = backupRoot + QLatin1Char('/') + relative;
            if (!copyFile(destination, backupPath, &error)) {
                qCritical().noquote() << "Updater:" << error;
                return 2;
            }
        }

        if (!copyFile(path, destination, &error)) {
            qCritical().noquote() << "Updater:" << error;

            // Откат: возвращаем сохранённые файлы.
            for (const QString &backup : collectFiles(backupRoot)) {
                const QString relativeBackup = backupRoot + QLatin1Char('/');
                Q_UNUSED(relativeBackup)
                const QString rel = QDir(backupRoot).relativeFilePath(backup);
                QFile::copy(backup, targetDir.absoluteFilePath(rel));
            }
            return 3;
        }

        updated.append(relative);
    }

    QFile::remove(source + QStringLiteral("/apply.json"));

    if (!restart.isEmpty() && QFileInfo::exists(restart))
        QProcess::startDetached(restart, QStringList());

    return 0;
}
