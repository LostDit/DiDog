#include "app/AppPaths.h"
#include "app/Logging.h"
#include "app/SingleInstance.h"
#include "server/ServerApp.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

int main(int argc, char *argv[])
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

    parser.addOptions({portOption, dataOption, verboseOption});
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
    if (!server.start(static_cast<quint16>(port)))
        return 3;

    return app.exec();
}
