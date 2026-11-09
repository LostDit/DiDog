#include "app/CrashHandler.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace app {
namespace {

QString g_crashDir;

#ifdef Q_OS_WIN
LONG WINAPI unhandledFilter(EXCEPTION_POINTERS *info)
{
    if (g_crashDir.isEmpty())
        return EXCEPTION_EXECUTE_HANDLER;

    QDir().mkpath(g_crashDir);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString base = g_crashDir + QStringLiteral("/crash-") + stamp;

    const HANDLE file = CreateFileW(
        reinterpret_cast<const wchar_t *>((base + QStringLiteral(".dmp")).utf16()),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = info;
        dumpInfo.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          MiniDumpWithIndirectlyReferencedMemory, &dumpInfo, nullptr, nullptr);
        CloseHandle(file);
    }

    QFile report(base + QStringLiteral(".txt"));
    if (report.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
        const void *address = info && info->ExceptionRecord
                                  ? info->ExceptionRecord->ExceptionAddress
                                  : nullptr;
        report.write(QStringLiteral("DiDog crash report\n"
                                    "time: %1\nexception: 0x%2\naddress: %3\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                         .arg(code, 8, 16, QLatin1Char('0'))
                         .arg(reinterpret_cast<quintptr>(address), 0, 16)
                         .toUtf8());
        report.close();
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

void CrashHandler::install(const QString &dataDir)
{
    g_crashDir = dataDir + QStringLiteral("/crashes");
    QDir().mkpath(g_crashDir);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledFilter);
#endif
}

} // namespace app
