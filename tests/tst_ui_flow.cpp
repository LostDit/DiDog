#include "app/AppPaths.h"
#include "app/ClientController.h"
#include "app/Localization.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "server/ServerApp.h"
#include "ui/ComposerWidget.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/MessageBubble.h"
#include "ui/SearchDialog.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMovie>
#include <QPushButton>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class TestUiFlow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void twoClientsExchangeMessages();
    void historySurvivesRestart();
    void sendsImageAttachment();
    void searchesMessages();
    void englishInterface();

private:
    QLineEdit *editWithPlaceholder(QWidget *root, const QString &placeholder) const;
    QPushButton *buttonWithText(QWidget *root, const QString &text) const;

    ui::LoginWindow *findLoginWindow() const;
    ui::MainWindow *findMainWindow(const QString &displayName) const;

    // Регистрирует клиента и возвращает его главное окно (nullptr при ошибке).
    ui::MainWindow *registerClient(app::SettingsStore *settings,
                                   const QString &dataDir,
                                   const QString &username,
                                   const QString &displayName);

    // Поднимает клиента с сохранённым токеном (без формы входа).
    ui::MainWindow *resumeClient(app::SettingsStore *settings, const QString &dataDir,
                                 const QString &displayName);

    bool windowHasBubble(ui::MainWindow *window, const QString &textPart) const;
    bool windowHasImage(ui::MainWindow *window) const;

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<server::ServerApp> m_server;
    QScopedPointer<app::SettingsStore> m_aliceSettings;
    QScopedPointer<app::SettingsStore> m_bobSettings;
    QScopedPointer<app::ThemeManager> m_theme;
    QList<app::ClientController *> m_controllers;
};

void TestUiFlow::init()
{
    m_dir.reset(new QTemporaryDir);
    QVERIFY(m_dir->isValid());

    m_server.reset(new server::ServerApp);
    QString error;
    QVERIFY2(m_server->initialize(m_dir->filePath(QStringLiteral("server.db")),
                                  m_dir->filePath(QStringLiteral("attachments")), &error),
             qPrintable(error));
    QVERIFY2(m_server->start(0), qPrintable(m_server->errorString()));

    m_aliceSettings.reset(new app::SettingsStore(m_dir->filePath(QStringLiteral("alice.ini"))));
    m_aliceSettings->setServerHost(QStringLiteral("127.0.0.1"));
    m_aliceSettings->setServerPort(m_server->port());

    m_bobSettings.reset(new app::SettingsStore(m_dir->filePath(QStringLiteral("bob.ini"))));
    m_bobSettings->setServerHost(QStringLiteral("127.0.0.1"));
    m_bobSettings->setServerPort(m_server->port());

    m_theme.reset(new app::ThemeManager(m_aliceSettings.data()));
    m_theme->setTheme(app::ThemeManager::Theme::Light);
    m_theme->apply();
}

void TestUiFlow::cleanup()
{
    qDeleteAll(m_controllers);
    m_controllers.clear();

    for (QWidget *widget : qApp->topLevelWidgets()) {
        if (qobject_cast<ui::LoginWindow *>(widget) || qobject_cast<ui::MainWindow *>(widget))
            widget->close();
    }
    QTest::qWait(50);

    m_theme.reset();
    m_aliceSettings.reset();
    m_bobSettings.reset();
    m_server.reset();
    app::paths::setOverrideDataDir(QString());
    app::i18n::install(QStringLiteral("ru"));
    m_dir.reset();
}

QLineEdit *TestUiFlow::editWithPlaceholder(QWidget *root, const QString &placeholder) const
{
    for (QLineEdit *edit : root->findChildren<QLineEdit *>()) {
        if (edit->placeholderText().startsWith(placeholder))
            return edit;
    }
    return nullptr;
}

QPushButton *TestUiFlow::buttonWithText(QWidget *root, const QString &text) const
{
    for (QPushButton *button : root->findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

ui::LoginWindow *TestUiFlow::findLoginWindow() const
{
    for (QWidget *widget : qApp->topLevelWidgets()) {
        auto *login = qobject_cast<ui::LoginWindow *>(widget);
        if (login && login->isVisible())
            return login;
    }
    return nullptr;
}

ui::MainWindow *TestUiFlow::findMainWindow(const QString &displayName) const
{
    for (QWidget *widget : qApp->topLevelWidgets()) {
        auto *window = qobject_cast<ui::MainWindow *>(widget);
        if (!window || !window->isVisible())
            continue;
        auto *name = window->findChild<QLabel *>(QStringLiteral("ProfileName"));
        if (name && name->text() == displayName)
            return window;
    }
    return nullptr;
}

bool TestUiFlow::windowHasBubble(ui::MainWindow *window, const QString &textPart) const
{
    if (!window)
        return false;

    for (const ui::MessageBubble *bubble : window->findChildren<ui::MessageBubble *>()) {
        for (const QLabel *label : bubble->findChildren<QLabel *>()) {
            if (label->text().contains(textPart))
                return true;
        }
    }
    return false;
}

ui::MainWindow *TestUiFlow::registerClient(app::SettingsStore *settings,
                                           const QString &dataDir,
                                           const QString &username,
                                           const QString &displayName)
{
    // Каждый клиент в тесте получает собственный каталог данных (история, кэш).
    app::paths::setOverrideDataDir(dataDir);
    QDir().mkpath(dataDir);

    auto *controller = new app::ClientController(settings, m_theme.data(), QString(), this);
    m_controllers.append(controller);
    controller->start();

    auto waitFor = [](auto predicate, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (predicate())
                return true;
            QTest::qWait(20);
        }
        return predicate();
    };

    ui::LoginWindow *login = nullptr;
    if (!waitFor([this, &login]() { return (login = findLoginWindow()) != nullptr; }, 5000)) {
        qWarning("Окно входа не появилось");
        return nullptr;
    }

    auto *status = login->findChild<QLabel *>(QStringLiteral("LoginStatus"));
    if (!waitFor([status]() {
            return status && status->text().contains(QStringLiteral("доступен"));
        }, 8000)) {
        qWarning().noquote() << "Нет соединения с сервером, статус:"
                             << (status ? status->text() : QStringLiteral("<нет>"));
        return nullptr;
    }

    if (auto *registerTab = buttonWithText(login, QStringLiteral("Регистрация")))
        QTest::mouseClick(registerTab, Qt::LeftButton);

    QLineEdit *loginEdit = editWithPlaceholder(login, QStringLiteral("Логин"));
    QLineEdit *nameEdit = editWithPlaceholder(login, QStringLiteral("Как вас"));
    QLineEdit *password = editWithPlaceholder(login, QStringLiteral("Пароль"));
    if (!loginEdit || !nameEdit || !password) {
        qWarning("Не найдены поля формы входа");
        return nullptr;
    }

    QTest::keyClicks(loginEdit, username);
    nameEdit->setText(displayName); // кириллица не набирается через keyClicks
    QTest::keyClicks(password, QStringLiteral("password123"));

    auto *submit = buttonWithText(login, QStringLiteral("Создать аккаунт"));
    if (!submit) {
        qWarning("Не найдена кнопка регистрации");
        return nullptr;
    }
    QTest::mouseClick(submit, Qt::LeftButton);

    ui::MainWindow *window = nullptr;
    if (!waitFor([this, &window, &displayName]() {
            return (window = findMainWindow(displayName)) != nullptr;
        }, 15000)) {
        auto *errorLabel = login->findChild<QLabel *>(QStringLiteral("ErrorLabel"));
        qWarning().noquote() << "Главное окно не открылось, ошибка:"
                             << (errorLabel ? errorLabel->text() : QStringLiteral("<нет>"));
        return nullptr;
    }
    return window;
}

void TestUiFlow::twoClientsExchangeMessages()
{
    ui::MainWindow *alice = registerClient(m_aliceSettings.data(),
                                           m_dir->filePath(QStringLiteral("alice-data")),
                                           QStringLiteral("alice"), QStringLiteral("Алиса"));
    QVERIFY(alice);
    QTest::qWait(300);

    ui::MainWindow *bob = registerClient(m_bobSettings.data(),
                                         m_dir->filePath(QStringLiteral("bob-data")),
                                         QStringLiteral("bob"), QStringLiteral("Боб"));
    QVERIFY(bob);
    QTest::qWait(400);

    const QString shotDir = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("screenshots"));
    QDir().mkpath(shotDir);

    // Алиса пишет в общий чат.
    auto *aliceComposer = alice->findChild<ui::ComposerEdit *>();
    QVERIFY(aliceComposer);
    QTRY_VERIFY_WITH_TIMEOUT(aliceComposer->isEnabled(), 5000);

    aliceComposer->setPlainText(QStringLiteral("Привет! Это тестовое сообщение в общий чат."));
    QTest::keyClick(aliceComposer, Qt::Key_Return);

    QTRY_VERIFY_WITH_TIMEOUT(windowHasBubble(alice, QStringLiteral("тестовое сообщение")), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(windowHasBubble(bob, QStringLiteral("тестовое сообщение")), 10000);

    // Боб отвечает.
    auto *bobComposer = bob->findChild<ui::ComposerEdit *>();
    QVERIFY(bobComposer);
    bobComposer->setPlainText(QStringLiteral("Привет, Алиса! Сообщение получено."));
    QTest::keyClick(bobComposer, Qt::Key_Return);

    QTRY_VERIFY_WITH_TIMEOUT(windowHasBubble(alice, QStringLiteral("Сообщение получено")), 10000);

    QTest::qWait(500);
    alice->grab().save(shotDir + QStringLiteral("/chat-light.png"));

    m_theme->setTheme(app::ThemeManager::Theme::Dark);
    QTest::qWait(400);
    alice->grab().save(shotDir + QStringLiteral("/chat-dark.png"));

    bob->grab().save(shotDir + QStringLiteral("/chat-bob-dark.png"));
}

bool TestUiFlow::windowHasImage(ui::MainWindow *window) const
{
    if (!window)
        return false;

    for (const ui::MessageBubble *bubble : window->findChildren<ui::MessageBubble *>()) {
        for (const QLabel *label : bubble->findChildren<QLabel *>()) {
            if (label->objectName() != QLatin1String("AttachmentImage"))
                continue;
            if (label->pixmap() && !label->pixmap()->isNull())
                return true;
            if (label->movie() && label->movie()->isValid())
                return true;
        }
    }
    return false;
}

ui::MainWindow *TestUiFlow::resumeClient(app::SettingsStore *settings, const QString &dataDir,
                                         const QString &displayName)
{
    app::paths::setOverrideDataDir(dataDir);

    auto *controller = new app::ClientController(settings, m_theme.data(), QString(), this);
    m_controllers.append(controller);
    controller->start();

    QElapsedTimer timer;
    timer.start();
    ui::MainWindow *window = nullptr;
    while (timer.elapsed() < 10000) {
        if ((window = findMainWindow(displayName)) != nullptr)
            return window;
        QTest::qWait(20);
    }

    qWarning("Не удалось восстановить сессию по сохранённому токену");
    return nullptr;
}

void TestUiFlow::historySurvivesRestart()
{
    const QString dataDir = m_dir->filePath(QStringLiteral("alice-data"));

    ui::MainWindow *alice = registerClient(m_aliceSettings.data(), dataDir,
                                           QStringLiteral("alice"), QStringLiteral("Алиса"));
    QVERIFY(alice);
    QTest::qWait(400);

    auto *composer = alice->findChild<ui::ComposerEdit *>();
    QVERIFY(composer);
    QTRY_VERIFY_WITH_TIMEOUT(composer->isEnabled(), 5000);

    composer->setPlainText(QStringLiteral("История переживёт перезапуск."));
    QTest::keyClick(composer, Qt::Key_Return);

    QTRY_VERIFY_WITH_TIMEOUT(windowHasBubble(alice, QStringLiteral("переживёт перезапуск")),
                             10000);

    // Даём рабочему потоку записать сообщение, затем закрываем клиента.
    QTest::qWait(600);
    QVERIFY(!m_controllers.isEmpty());
    delete m_controllers.takeFirst();
    QTest::qWait(300);

    ui::MainWindow *restored = resumeClient(m_aliceSettings.data(), dataDir,
                                            QStringLiteral("Алиса"));
    QVERIFY(restored);

    QTRY_VERIFY_WITH_TIMEOUT(windowHasBubble(restored, QStringLiteral("переживёт перезапуск")),
                             10000);
}

void TestUiFlow::sendsImageAttachment()
{
    const QString aliceDir = m_dir->filePath(QStringLiteral("alice-attachment-data"));
    const QString bobDir = m_dir->filePath(QStringLiteral("bob-attachment-data"));

    ui::MainWindow *alice = registerClient(m_aliceSettings.data(), aliceDir,
                                           QStringLiteral("alice"), QStringLiteral("Алиса"));
    QVERIFY(alice);

    ui::MainWindow *bob = registerClient(m_bobSettings.data(), bobDir,
                                         QStringLiteral("bob"), QStringLiteral("Боб"));
    QVERIFY(bob);
    QTest::qWait(400);

    QImage image(96, 64, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#3B7DF6")));
    const QString imagePath = m_dir->filePath(QStringLiteral("picture.png"));
    QVERIFY(image.save(imagePath));

    alice->sendFile(imagePath);

    QTRY_VERIFY_WITH_TIMEOUT(windowHasImage(alice), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(windowHasImage(bob), 15000);

    // Файл скачан в кэш Боба: attachments/<ab>/<sha256>.
    QStringList cached;
    QDirIterator iterator(bobDir + QStringLiteral("/attachments"), QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
        cached.append(iterator.next());

    QVERIFY2(!cached.isEmpty(), "файл не попал в кэш получателя");

    const QString shotDir = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("screenshots"));
    QDir().mkpath(shotDir);
    QTest::qWait(400);
    bob->grab().save(shotDir + QStringLiteral("/chat-image.png"));
}

void TestUiFlow::searchesMessages()
{
    const QString dataDir = m_dir->filePath(QStringLiteral("alice-search-data"));
    ui::MainWindow *alice = registerClient(m_aliceSettings.data(), dataDir,
                                           QStringLiteral("alice"), QStringLiteral("Алиса"));
    QVERIFY(alice);
    QTest::qWait(300);

    auto *composer = alice->findChild<ui::ComposerEdit *>();
    QVERIFY(composer);
    QTRY_VERIFY_WITH_TIMEOUT(composer->isEnabled(), 5000);

    composer->setPlainText(QStringLiteral("Уникальное слово для поиска"));
    QTest::keyClick(composer, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(windowHasBubble(alice, QStringLiteral("Уникальное")), 10000);

    // Даём локальной истории записать сообщение.
    QTest::qWait(600);

    alice->openSearch();
    auto *dialog = alice->findChild<ui::SearchDialog *>();
    QVERIFY(dialog);

    auto *query = dialog->findChild<QLineEdit *>();
    auto *results = dialog->findChild<QListWidget *>();
    QVERIFY(query);
    QVERIFY(results);

    query->setText(QStringLiteral("Уникальное")); // кириллица не набирается через keyClicks
    QTRY_VERIFY_WITH_TIMEOUT(results->count() > 0, 5000);

    QTest::qWait(150);

    const QString shotDir = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("screenshots"));
    QDir().mkpath(shotDir);
    dialog->grab().save(shotDir + QStringLiteral("/search-results.png"));

    results->setCurrentRow(0);
    QTest::keyClick(results, Qt::Key_Return);

    QTRY_VERIFY_WITH_TIMEOUT(!dialog->isVisible(), 2000);
}

void TestUiFlow::englishInterface()
{
    app::i18n::install(QStringLiteral("en"));
    QCOMPARE(app::i18n::resolvedLanguage(), QStringLiteral("en"));
    QCOMPARE(QCoreApplication::translate("ui::LoginWindow", "Вход"), QStringLiteral("Sign in"));

    app::paths::setOverrideDataDir(m_dir->filePath(QStringLiteral("english-data")));
    m_aliceSettings->setLanguage(QStringLiteral("en"));

    auto *controller = new app::ClientController(m_aliceSettings.data(), m_theme.data(), QString(), this);
    m_controllers.append(controller);
    controller->start();

    ui::LoginWindow *login = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((login = findLoginWindow()) != nullptr, 5000);

    auto *loginEdit = editWithPlaceholder(login, QStringLiteral("Login"));
    QVERIFY2(loginEdit, "английский перевод не применился к форме входа");

    const QString shotDir = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("screenshots"));
    QDir().mkpath(shotDir);
    QTest::qWait(300);
    login->grab().save(shotDir + QStringLiteral("/login-en.png"));

    app::i18n::install(QStringLiteral("ru"));
}

QTEST_MAIN(TestUiFlow)
#include "tst_ui_flow.moc"
