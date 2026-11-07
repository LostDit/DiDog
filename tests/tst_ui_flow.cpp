#include "app/ClientController.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "server/ServerApp.h"
#include "ui/ComposerWidget.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/MessageBubble.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
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

private:
    QLineEdit *editWithPlaceholder(QWidget *root, const QString &placeholder) const;
    QPushButton *buttonWithText(QWidget *root, const QString &text) const;

    ui::LoginWindow *findLoginWindow() const;
    ui::MainWindow *findMainWindow(const QString &displayName) const;

    // Регистрирует клиента и возвращает его главное окно (nullptr при ошибке).
    ui::MainWindow *registerClient(app::SettingsStore *settings,
                                   const QString &username,
                                   const QString &displayName);

    bool windowHasBubble(ui::MainWindow *window, const QString &textPart) const;

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
                                           const QString &username,
                                           const QString &displayName)
{
    auto *controller = new app::ClientController(settings, m_theme.data(), this);
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
    ui::MainWindow *alice = registerClient(m_aliceSettings.data(), QStringLiteral("alice"),
                                           QStringLiteral("Алиса"));
    QVERIFY(alice);
    QTest::qWait(300);

    ui::MainWindow *bob = registerClient(m_bobSettings.data(), QStringLiteral("bob"),
                                         QStringLiteral("Боб"));
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

QTEST_MAIN(TestUiFlow)
#include "tst_ui_flow.moc"
