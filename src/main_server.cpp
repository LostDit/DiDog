#include "app/AppPaths.h"
#include "app/Logging.h"
#include "app/SingleInstance.h"
#include "server/ServerApp.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QVector>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

// Общая часть: разбор аргументов, инициализация и запуск сервера.
int runServer(int argc, char *argv[]);

#ifdef Q_OS_WIN

SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
SERVICE_STATUS g_serviceStatus{};

void reportServiceState(DWORD state, DWORD exitCode = 0)
{
    if (!g_serviceStatusHandle)
        return;

    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwCurrentState = state;
    g_serviceStatus.dwWin32ExitCode = exitCode;
    g_serviceStatus.dwControlsAccepted = (state == SERVICE_RUNNING) ? SERVICE_ACCEPT_STOP : 0;
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}

void WINAPI serviceControlHandler(DWORD control)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        reportServiceState(SERVICE_STOP_PENDING);
        QCoreApplication::quit();
    }
}

void WINAPI serviceMain(DWORD argc, LPWSTR *argv)
{
    const wchar_t *serviceName = L"MessengerAppServer";

    g_serviceStatusHandle = RegisterServiceCtrlHandlerW(serviceName, serviceControlHandler);
    if (!g_serviceStatusHandle)
        return;

    reportServiceState(SERVICE_RUNNING);

    // Аргументы сервиса приходят в UTF-16; переводим в привычный char** для runServer.
    QVector<QByteArray> storage;
    storage.reserve(static_cast<int>(argc));
    for (DWORD i = 0; i < argc; ++i)
        storage.append(QString::fromWCharArray(argv[i]).toUtf8());

    QVector<char *> pointers;
    pointers.reserve(storage.size());
    for (QByteArray &value : storage)
        pointers.append(value.data());

    const int exitCode = runServer(pointers.size(), pointers.data());

    reportServiceState(SERVICE_STOPPED, static_cast<DWORD>(exitCode));
}

bool hasArgument(int argc, char *argv[], const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0)
            return true;
    }
    return false;
}

QString serviceExecutablePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

// Собирает командную строку сервиса: добавляет --service и каталог данных.
QString serviceCommandLine(const QStringList &arguments, QString *error)
{
    QStringList filtered;
    bool hasData = false;

    for (int i = 0; i < arguments.size(); ++i) {
        const QString &value = arguments.at(i);
        if (value == QLatin1String("--install-service")
            || value == QLatin1String("--uninstall-service")) {
            continue;
        }
        if (value == QLatin1String("--data"))
            hasData = true;
        filtered.append(value);
    }

    if (!hasData) {
        QString programData = qEnvironmentVariable("ProgramData");
        if (programData.isEmpty()) {
            if (error)
                *error = QStringLiteral("Не удалось определить каталог ProgramData");
            return QString();
        }

        const QString dataDir = QDir::toNativeSeparators(programData + QStringLiteral("/MessengerApp"));
        if (!QDir().mkpath(dataDir)) {
            if (error)
                *error = QStringLiteral("Не удалось создать каталог данных: %1").arg(dataDir);
            return QString();
        }

        filtered.append(QStringLiteral("--data"));
        filtered.append(dataDir);
    }

    QStringList parts;
    parts.append(QLatin1Char('"') + serviceExecutablePath() + QLatin1Char('"'));
    parts.append(QStringLiteral("--service"));

    for (const QString &argument : filtered) {
        if (argument.contains(QLatin1Char(' ')))
            parts.append(QLatin1Char('"') + argument + QLatin1Char('"'));
        else
            parts.append(argument);
    }

    return parts.join(QLatin1Char(' '));
}

bool installService(const QStringList &arguments, QString *error)
{
    const QString commandLine = serviceCommandLine(arguments, error);
    if (commandLine.isEmpty())
        return false;

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!manager) {
        const DWORD code = GetLastError();
        if (error) {
            *error = code == ERROR_ACCESS_DENIED
                         ? QStringLiteral("Требуются права администратора.")
                         : QStringLiteral("OpenSCManager: код %1").arg(code);
        }
        return false;
    }

    const std::wstring name = QStringLiteral("MessengerAppServer").toStdWString();
    const std::wstring displayName = QStringLiteral("DiDog Server").toStdWString();
    const std::wstring command = commandLine.toStdWString();

    SC_HANDLE service = CreateServiceW(
        manager, name.c_str(), displayName.c_str(), SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!service) {
        const DWORD code = GetLastError();
        CloseServiceHandle(manager);
        if (error) {
            *error = code == ERROR_ACCESS_DENIED
                         ? QStringLiteral("Требуются права администратора.")
                         : (code == ERROR_SERVICE_EXISTS
                                ? QStringLiteral("Служба уже установлена.")
                                : QStringLiteral("CreateService: код %1").arg(code));
        }
        return false;
    }

    const std::wstring description =
        QStringLiteral("Сервер DiDog: обмен сообщениями и вложениями").toStdWString();
    SERVICE_DESCRIPTIONW descriptionData{const_cast<wchar_t *>(description.c_str())};
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &descriptionData);

    const bool started = StartServiceW(service, 0, nullptr) != FALSE;
    const DWORD startCode = started ? 0 : GetLastError();

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    if (!started && startCode != ERROR_SERVICE_ALREADY_RUNNING) {
        if (error)
            *error = QStringLiteral("Служба установлена, но не запустилась: код %1").arg(startCode);
        return false;
    }

    return true;
}

bool uninstallService(QString *error)
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        if (error)
            *error = QStringLiteral("Требуются права администратора.");
        return false;
    }

    const std::wstring name = QStringLiteral("MessengerAppServer").toStdWString();
    SC_HANDLE service = OpenServiceW(manager, name.c_str(),
                                     SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);

    if (!service) {
        const DWORD code = GetLastError();
        CloseServiceHandle(manager);
        if (error) {
            *error = code == ERROR_SERVICE_DOES_NOT_EXIST
                         ? QStringLiteral("Служба не установлена.")
                         : QStringLiteral("OpenService: код %1").arg(code);
        }
        return false;
    }

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);

    const bool deleted = DeleteService(service) != FALSE;
    const DWORD deleteCode = GetLastError();

    CloseServiceHandle(service);
    CloseServiceHandle(manager);

    if (!deleted) {
        if (error)
            *error = QStringLiteral("DeleteService: код %1").arg(deleteCode);
        return false;
    }

    return true;
}

#endif // Q_OS_WIN

int runServer(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Консоль Windows по умолчанию не в UTF-8 — без этого русский текст превращается в «?????».
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MessengerApp"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MESSENGER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MessengerApp — сервер мессенджера"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Порт прослушивания (по умолчанию 45000)."),
        QStringLiteral("port"), QStringLiteral("45000"));
    const QCommandLineOption dataOption(
        QStringLiteral("data"),
        QStringLiteral("Каталог данных приложения."),
        QStringLiteral("dir"));
    const QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QStringLiteral("Подробное логирование (включая DEBUG) в консоль."));
    const QCommandLineOption tlsOption(
        QStringLiteral("tls"),
        QStringLiteral("Включить TLS (шифрование соединений)."));
    const QCommandLineOption tlsCertOption(
        QStringLiteral("tls-cert"),
        QStringLiteral("Путь к сертификату сервера (PEM). По умолчанию tls/server.crt."),
        QStringLiteral("path"));
    const QCommandLineOption tlsKeyOption(
        QStringLiteral("tls-key"),
        QStringLiteral("Путь к приватному ключу сервера (PEM). По умолчанию tls/server.key."),
        QStringLiteral("path"));
    const QCommandLineOption serviceOption(
        QStringLiteral("service"),
        QStringLiteral("Внутренний режим: запуск под управлением службы Windows."));
    const QCommandLineOption addPromoOption(
        QStringLiteral("add-promo"),
        QStringLiteral("Создать промокод CODE:DAYS[:MAX] и выйти (администрирование)."),
        QStringLiteral("spec"));
    const QCommandLineOption voicePortOption(
        QStringLiteral("voice-port"),
        QStringLiteral("UDP-порт голосового релея (по умолчанию 45001)."),
        QStringLiteral("port"), QStringLiteral("45001"));
    const QCommandLineOption updatesOption(
        QStringLiteral("updates-dir"),
        QStringLiteral("Каталог обновлений с manifest.json."),
        QStringLiteral("dir"));

    parser.addOptions({portOption, dataOption, verboseOption, tlsOption, tlsCertOption,
                       tlsKeyOption, serviceOption, addPromoOption, voicePortOption,
                       updatesOption});
    parser.process(app);

    bool portOk = false;
    const uint port = parser.value(portOption).toUInt(&portOk);
    if (!portOk || port == 0 || port > 65535) {
        qCritical().noquote() << QStringLiteral("Некорректный порт: %1").arg(parser.value(portOption));
        return 1;
    }

    if (parser.isSet(dataOption))
        app::paths::setOverrideDataDir(parser.value(dataOption));

    QString pathError;
    if (!app::paths::ensureDirectories(&pathError)) {
        qCritical().noquote() << pathError;
        return 1;
    }

    app::logging::install(app::paths::logsDir(), parser.isSet(verboseOption), true);
    qInfo().noquote() << QStringLiteral("Сервер %1, данные: %2")
                             .arg(QStringLiteral(MESSENGER_VERSION), app::paths::dataDir());

    app::SingleInstance instanceGuard(app::paths::dataDir() + QStringLiteral("/server.lock"));
    if (!instanceGuard.tryAcquire()) {
        qCritical().noquote() << instanceGuard.errorString();
        return 2;
    }

    server::ServerApp server;

    bool voicePortOk = false;
    const uint voicePort = parser.value(voicePortOption).toUInt(&voicePortOk);
    if (voicePortOk && voicePort > 0 && voicePort <= 65535)
        server.setVoicePort(static_cast<quint16>(voicePort));

    if (parser.isSet(updatesOption))
        server.setUpdatesRoot(parser.value(updatesOption));

    if (parser.isSet(tlsOption)) {
        const auto findFile = [](const QString &explicitPath, const QString &relativeName) {
            if (!explicitPath.isEmpty())
                return QFileInfo::exists(explicitPath) ? explicitPath : QString();

            const QStringList candidates = {
                QCoreApplication::applicationDirPath() + QLatin1Char('/') + relativeName,
                app::paths::dataDir() + QLatin1Char('/') + relativeName,
            };

            for (const QString &candidate : candidates) {
                if (QFileInfo::exists(candidate))
                    return candidate;
            }
            return QString();
        };

        const QString certificatePath = findFile(parser.value(tlsCertOption),
                                                 QStringLiteral("tls/server.crt"));
        const QString keyPath = findFile(parser.value(tlsKeyOption),
                                         QStringLiteral("tls/server.key"));

        if (certificatePath.isEmpty() || keyPath.isEmpty()) {
            qCritical().noquote()
                << QStringLiteral("TLS: не найден сертификат или ключ. Укажите --tls-cert "
                                  "и --tls-key (см. scripts/make-cert.ps1).");
            return 5;
        }

        QString tlsError;
        if (!server.enableTls(certificatePath, keyPath, &tlsError)) {
            qCritical().noquote() << QStringLiteral("TLS: %1").arg(tlsError);
            return 5;
        }

        qInfo().noquote() << QStringLiteral("TLS включён, сертификат: %1").arg(certificatePath);
    }

    QString serverError;
    if (!server.initialize(app::paths::serverDbPath(), app::paths::attachmentsDir(),
                           &serverError)) {
        qCritical().noquote() << QStringLiteral("Не удалось инициализировать сервер: %1")
                                     .arg(serverError);
        return 3;
    }

    if (parser.isSet(addPromoOption)) {
        const QStringList parts = parser.value(addPromoOption).split(QLatin1Char(':'));
        bool daysOk = false;
        const int days = parts.size() > 1 ? parts.at(1).toInt(&daysOk) : 0;
        const QString code = parts.value(0).trimmed().toUpper();

        if (code.isEmpty() || !daysOk || days <= 0) {
            qCritical().noquote() << QStringLiteral("Формат: --add-promo CODE:DAYS[:MAX]");
            return 8;
        }

        const int maxUses = parts.size() > 2 ? parts.at(2).toInt() : 0;
        QString promoError;
        if (!server.createPromo(code, QStringLiteral("plus"), days, maxUses, &promoError)) {
            qCritical().noquote() << QStringLiteral("Промокод: %1").arg(promoError);
            return 8;
        }

        qInfo().noquote() << QStringLiteral("Промокод %1 создан: %2 дней, лимит %3")
                                 .arg(code)
                                 .arg(days)
                                 .arg(maxUses > 0 ? QString::number(maxUses)
                                                  : QStringLiteral("без ограничения"));
        return 0;
    }

    if (!server.start(static_cast<quint16>(port)))
        return 4;

    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);

    const bool installRequested = hasArgument(argc, argv, "--install-service");
    const bool uninstallRequested = hasArgument(argc, argv, "--uninstall-service");

    if (installRequested || uninstallRequested) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("MessengerApp"));

        QStringList arguments;
        for (int i = 1; i < argc; ++i)
            arguments.append(QString::fromLocal8Bit(argv[i]));

        QString error;
        const bool ok = uninstallRequested ? uninstallService(&error)
                                           : installService(arguments, &error);

        if (ok) {
            qInfo().noquote() << (uninstallRequested
                                      ? QStringLiteral("Служба MessengerAppServer удалена.")
                                      : QStringLiteral("Служба MessengerAppServer установлена "
                                                       "и запущена."));
            return 0;
        }

        qCritical().noquote() << QStringLiteral("Служба: %1").arg(error);
        return 6;
    }

    if (hasArgument(argc, argv, "--service")) {
        SERVICE_TABLE_ENTRYW table[] = {
            {const_cast<LPWSTR>(L"MessengerAppServer"), serviceMain},
            {nullptr, nullptr},
        };

        if (!StartServiceCtrlDispatcherW(table)) {
            const DWORD code = GetLastError();
            qCritical().noquote()
                << QStringLiteral("Не удалось подключиться к диспетчеру служб (код %1)")
                       .arg(code);
            return 7;
        }
        return 0;
    }
#endif

    return runServer(argc, argv);
}
