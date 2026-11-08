#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

QFile *g_log = nullptr;

void logLine(const QString &text)
{
    if (!g_log)
        return;

    g_log->write((QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss "))
                  + text + QLatin1Char('\n'))
                     .toUtf8());
    g_log->flush();
}

void notify(const QString &title, const QString &text)
{
    logLine(QStringLiteral("ERROR: %1 — %2").arg(title, text));
#ifdef Q_OS_WIN
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(text.utf16()),
                reinterpret_cast<LPCWSTR>(title.utf16()), MB_ICONERROR | MB_OK);
#endif
}

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

bool startApplication(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;

    const QString workingDir = QFileInfo(path).absolutePath();
    if (QProcess::startDetached(path, QStringList(), workingDir)) {
        logLine(QStringLiteral("Запущено: %1").arg(path));
        return true;
    }

    logLine(QStringLiteral("startDetached не сработал, пробуем ShellExecute: %1").arg(path));
#ifdef Q_OS_WIN
    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open", reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
        reinterpret_cast<LPCWSTR>(workingDir.utf16()), SW_SHOWNORMAL);
    if (reinterpret_cast<qintptr>(result) > 32) {
        logLine(QStringLiteral("ShellExecute запустил приложение"));
        return true;
    }
#endif
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MessengerUpdater"));

    const QString logPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                            + QStringLiteral("/MessengerUpdater.log");
    QFile logFile(logPath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        g_log = &logFile;

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption sourceOption(QStringLiteral("source"),
                                    QStringLiteral("Каталог с новыми файлами"),
                                    QStringLiteral("dir"));
    QCommandLineOption targetOption(QStringLiteral("target"),
                                    QStringLiteral("Каталог установки"),
                                    QStringLiteral("dir"));
    QCommandLineOption restartOption(QStringLiteral("restart"),
                                     QStringLiteral("Что запустить после обновления"),
                                     QStringLiteral("exe"));
    QCommandLineOption pidOption(QStringLiteral("pid"),
                                 QStringLiteral("PID процесса, который нужно дождаться"),
                                 QStringLiteral("pid"), QStringLiteral("0"));
    parser.addOptions({sourceOption, targetOption, restartOption, pidOption});
    parser.process(app);

    const QString source = parser.value(sourceOption);
    const QString target = parser.value(targetOption);
    const QString restart = parser.value(restartOption);
    const qint64 pid = parser.value(pidOption).toLongLong();

    logLine(QStringLiteral("--- старт: source=%1 target=%2 restart=%3 pid=%4")
                .arg(source, target, restart)
                .arg(pid));

    if (source.isEmpty() || target.isEmpty()) {
        notify(QStringLiteral("MessengerApp"), QStringLiteral("Обновление: не заданы каталоги."));
        return 1;
    }

    if (pid > 0 && !waitForProcess(pid, 60000)) {
        notify(QStringLiteral("MessengerApp"),
               QStringLiteral("Приложение не завершилось за 60 секунд. "
                              "Закройте его и повторите обновление."));
        return 1;
    }

    logLine(QStringLiteral("Приложение завершилось, начинаем замену файлов"));
    QThread::msleep(400);

    const QString backupRoot = source + QStringLiteral("/backup");
    const QDir targetDir(target);
    const QDir sourceDir(source);
    QString error;

    for (const QString &path : collectFiles(source)) {
        const QString relative = sourceDir.relativeFilePath(path);

        if (relative == QLatin1String("apply.json") || relative.startsWith(QLatin1String("backup/")))
            continue;

        const QString destination = targetDir.absoluteFilePath(relative);
        logLine(QStringLiteral("копируем %1 → %2").arg(relative, destination));

        if (QFileInfo::exists(destination)) {
            const QString backupPath = backupRoot + QLatin1Char('/') + relative;
            if (!copyFile(destination, backupPath, &error)) {
                notify(QStringLiteral("MessengerApp"), error);
                return 2;
            }
        }

        if (!copyFile(path, destination, &error)) {
            notify(QStringLiteral("MessengerApp"),
                   error + QStringLiteral("\n\nПодробности: ") + logPath);

            // Откат: возвращаем сохранённые файлы.
            for (const QString &backup : collectFiles(backupRoot)) {
                const QString rel = QDir(backupRoot).relativeFilePath(backup);
                QFile::copy(backup, targetDir.absoluteFilePath(rel));
            }
            return 3;
        }
    }

    QFile::remove(source + QStringLiteral("/apply.json"));
    logLine(QStringLiteral("Файлы заменены, перезапускаем приложение"));

    if (!startApplication(restart)) {
        notify(QStringLiteral("MessengerApp"),
               QStringLiteral("Файлы обновлены, но не удалось перезапустить приложение.\n"
                              "Запустите MessengerClient.exe вручную.\n\nПодробности: ")
                   + logPath);
        return 4;
    }

    logLine(QStringLiteral("Готово"));
    return 0;
}
