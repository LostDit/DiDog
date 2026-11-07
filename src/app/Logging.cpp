#include "app/Logging.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>
#include <cstdlib>

namespace app::logging {
namespace {

constexpr qint64 kMaxLogFileBytes = 5 * 1024 * 1024;
constexpr int kMaxLogFiles = 8;

struct LogState {
    QMutex mutex;
    QFile file;
    QString directory;
    QDate date;
    bool verbose = false;
    bool echoToConsole = false;
    bool installed = false;
    QtMessageHandler previous = nullptr;
};

LogState &state()
{
    static LogState instance;
    return instance;
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "INFO ";
}

void removeOldLogs(const QString &directory)
{
    QDir dir(directory);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("app-*.log")},
                                                  QDir::Files, QDir::Time);

    for (int i = kMaxLogFiles; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

bool openLogFile(LogState &s)
{
    if (s.file.isOpen())
        s.file.close();

    if (s.directory.isEmpty())
        return false;

    const QDate today = QDate::currentDate();
    QString path = QStringLiteral("%1/app-%2.log")
                       .arg(s.directory, today.toString(QStringLiteral("yyyy-MM-dd")));

    if (QFileInfo(path).size() >= kMaxLogFileBytes) {
        int index = 1;
        forever {
            const QString candidate = QStringLiteral("%1/app-%2.%3.log")
                                          .arg(s.directory,
                                               today.toString(QStringLiteral("yyyy-MM-dd")))
                                          .arg(index);
            if (!QFileInfo::exists(candidate) || QFileInfo(candidate).size() < kMaxLogFileBytes) {
                path = candidate;
                break;
            }
            ++index;
        }
    }

    s.file.setFileName(path);
    s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    s.date = today;

    if (s.file.isOpen())
        removeOldLogs(s.directory);

    return s.file.isOpen();
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    LogState &s = state();

    if (type == QtDebugMsg && !s.verbose)
        return;

    QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    line += QStringLiteral(" [") + QLatin1String(levelName(type)) + QStringLiteral("] ");
    line += message;

    if (context.category && qstrcmp(context.category, "default") != 0)
        line += QStringLiteral(" (%1)").arg(QLatin1String(context.category));

    {
        QMutexLocker locker(&s.mutex);

        const QDate today = QDate::currentDate();
        if (!s.file.isOpen() || s.date != today)
            openLogFile(s);

        if (s.file.isOpen()) {
            s.file.write(line.toUtf8());
            s.file.write("\n", 1);
            s.file.flush();
        }

        const bool important = (type == QtWarningMsg || type == QtCriticalMsg
                                || type == QtFatalMsg);
        if (s.verbose || s.echoToConsole || important) {
            // Консоль получает UTF-8: main_server выставляет кодовую страницу 65001.
            const QByteArray console = line.toUtf8() + '\n';
            std::fwrite(console.constData(), 1, static_cast<size_t>(console.size()), stderr);
            std::fflush(stderr);
        }
    }

    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

void install(const QString &logDirectory, bool verbose, bool echoToConsole)
{
    LogState &s = state();
    QMutexLocker locker(&s.mutex);

    s.directory = logDirectory;
    s.verbose = verbose;
    s.echoToConsole = echoToConsole;

    if (!s.installed) {
        s.previous = qInstallMessageHandler(messageHandler);
        s.installed = true;
    }

    openLogFile(s);
}

void shutdown()
{
    LogState &s = state();
    QMutexLocker locker(&s.mutex);

    if (s.installed) {
        qInstallMessageHandler(s.previous);
        s.installed = false;
    }
    if (s.file.isOpen())
        s.file.close();
}

void setVerbose(bool verbose)
{
    QMutexLocker locker(&state().mutex);
    state().verbose = verbose;
}

bool isVerbose()
{
    QMutexLocker locker(&state().mutex);
    return state().verbose;
}

QString currentLogFile()
{
    QMutexLocker locker(&state().mutex);
    return state().file.fileName();
}

} // namespace app::logging
