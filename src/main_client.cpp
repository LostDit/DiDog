#include "ui/ButtonFx.h"
#include "ui/StartupSplash.h"
#include "app/AppPaths.h"
#include "app/CrashHandler.h"
#include "app/ClientController.h"
#include "app/Localization.h"
#include "app/Logging.h"
#include "app/SettingsStore.h"
#include "app/SingleInstance.h"
#include "app/ThemeManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QEventLoop>
#include <QLocalSocket>
#include <QTimer>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStyleFactory>

namespace {

struct ClientOptions {
    QString profile;
    QString dataDir;
    bool verbose = false;
};

bool parseProfile(const QString &profile, QString *dataDir, QString *error)
{
    if (profile.isEmpty() || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]{1,32}$"))
                                  .match(profile).hasMatch()) {
        *error = QStringLiteral("Недопустимое имя профиля: используйте латиницу, цифры, «_», «-», «.».");
        return false;
    }

    // Базовый каталог вычисляется до подмены: профиль — подкаталог базового.
    *dataDir = app::paths::dataDir() + QStringLiteral("/profiles/") + profile;
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MessengerApp"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("DiDog"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MESSENGER_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/logo.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DiDog — клиент мессенджера"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption profileOption(
        QStringLiteral("profile"),
        QStringLiteral("Отдельный профиль: свой каталог данных (для второго клиента на машине)."),
        QStringLiteral("name"));
    const QCommandLineOption dataOption(
        QStringLiteral("data"),
        QStringLiteral("Каталог данных приложения."),
        QStringLiteral("dir"));
    const QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QStringLiteral("Подробное логирование в консоль."));

    parser.addOptions({profileOption, dataOption, verboseOption});
    parser.process(app);

    ClientOptions options;
    options.verbose = parser.isSet(verboseOption);

    if (parser.isSet(profileOption)) {
        QString error;
        if (!parseProfile(parser.value(profileOption), &options.dataDir, &error)) {
            QMessageBox::critical(nullptr, QStringLiteral("DiDog"), error);
            return 1;
        }
    }
    if (parser.isSet(dataOption))
        options.dataDir = parser.value(dataOption);

    if (!options.dataDir.isEmpty())
        app::paths::setOverrideDataDir(options.dataDir);

    QString pathError;
    if (!app::paths::ensureDirectories(&pathError)) {
        QMessageBox::critical(nullptr, QStringLiteral("DiDog"), pathError);
        return 1;
    }

    app::logging::install(app::paths::logsDir(), options.verbose);
    qInfo().noquote() << QStringLiteral("Клиент %1, данные: %2")
                             .arg(QStringLiteral(MESSENGER_VERSION), app::paths::dataDir());

    app::CrashHandler::install(app::paths::dataDir());

    app::SingleInstance instanceGuard(app::paths::dataDir() + QStringLiteral("/client.lock"));

    const QString instanceName = QStringLiteral("MessengerApp-")
        + QString::fromLatin1(QCryptographicHash::hash(app::paths::dataDir().toUtf8(),
                                                       QCryptographicHash::Sha1)
                                  .toHex()
                                  .left(16));

    if (!instanceGuard.tryAcquire()) {
        // Уже запущенный экземпляр: просим показать окно вместо второго трея.
        QLocalSocket socket;
        socket.connectToServer(instanceName);
        if (socket.waitForConnected(700)) {
            socket.write("show");
            socket.waitForBytesWritten(700);
            qInfo().noquote() << QStringLiteral("Активирован уже запущенный клиент");
            return 0;
        }

        qWarning().noquote() << instanceGuard.errorString();
        QMessageBox::information(nullptr, QStringLiteral("DiDog"),
                                 instanceGuard.errorString()
                                     + QStringLiteral("\n\nЗакройте уже запущенный клиент или "
                                                      "запустите второй с флагом --profile second."));
        return 0;
    }

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    ui::ButtonFx::install(&app);

    app::SettingsStore settings(app::paths::settingsFile());
    ui::ButtonFx::setEnabled(settings.animationsEnabled());
    app::i18n::install(settings.language());

    app::ThemeManager theme(&settings);
    theme.apply();

    // Сначала только заставка: окно входа появится после её исчезновения.
    QApplication::setQuitOnLastWindowClosed(false);

    ui::StartupSplash splash;
    splash.start();

    QEventLoop splashLoop;
    QObject::connect(&splash, &ui::StartupSplash::finished, &splashLoop, &QEventLoop::quit);
    QTimer::singleShot(3000, &splashLoop, &QEventLoop::quit);
    splashLoop.exec();

    QApplication::setQuitOnLastWindowClosed(true);

    app::ClientController controller(&settings, &theme, instanceName);
    controller.start();

    const int exitCode = app.exec();

    app::logging::shutdown();
    return exitCode;
}
