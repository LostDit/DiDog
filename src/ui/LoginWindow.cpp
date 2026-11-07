#include "ui/LoginWindow.h"

#include "app/SessionService.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {
namespace {

QLabel *makeLabel(const QString &text, const char *objectName, Qt::Alignment alignment)
{
    auto *label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setAlignment(alignment);
    return label;
}

} // namespace

LoginWindow::LoginWindow(net::ClientConnection *connection, app::SessionService *session,
                         app::SettingsStore *settings, app::ThemeManager *theme, QWidget *parent)
    : QWidget(parent)
    , m_connection(connection)
    , m_session(session)
    , m_settings(settings)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("Root"));
    setMinimumSize(760, 560);
    setWindowTitle(QStringLiteral("MessengerApp"));

    buildUi();
    applyMode();

    connect(m_session, &app::SessionService::authFailed, this, &LoginWindow::onAuthFailed);
    connect(m_session, &app::SessionService::authenticated, this, &LoginWindow::authenticated);
    connect(m_connection, &net::ClientConnection::stateChanged,
            this, &LoginWindow::onConnectionStateChanged);

    m_host->setText(m_settings->serverHost());
    m_port->setText(QString::number(m_settings->serverPort()));
    m_username->setText(m_settings->lastUserName());
}

void LoginWindow::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 18, 24, 24);
    outer->setSpacing(0);

    // Верхняя панель: переключатель темы.
    auto *topBar = new QHBoxLayout;
    topBar->addStretch(1);
    m_themeButton = new QToolButton(this);
    m_themeButton->setObjectName(QStringLiteral("IconButton"));
    m_themeButton->setIcon(QIcon(QStringLiteral(":/icons/moon.svg")));
    m_themeButton->setIconSize(QSize(20, 20));
    m_themeButton->setFixedSize(36, 36);
    m_themeButton->setCursor(Qt::PointingHandCursor);
    m_themeButton->setToolTip(tr("Переключить тему"));
    connect(m_themeButton, &QToolButton::clicked, this, [this]() {
        if (m_theme)
            m_theme->toggle();
        const bool dark = m_theme && m_theme->theme() == app::ThemeManager::Theme::Dark;
        m_themeButton->setIcon(QIcon(dark ? QStringLiteral(":/icons/sun.svg")
                                          : QStringLiteral(":/icons/moon.svg")));
    });
    topBar->addWidget(m_themeButton);
    outer->addLayout(topBar);

    outer->addStretch(1);

    auto *center = new QHBoxLayout;
    center->addStretch(1);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("LoginCard"));
    card->setFixedWidth(400);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(12);

    auto *logo = new QLabel(card);
    logo->setPixmap(QIcon(QStringLiteral(":/icons/logo.svg")).pixmap(52, 52));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    layout->addWidget(makeLabel(tr("MessengerApp"), "LoginTitle", Qt::AlignCenter));
    layout->addWidget(makeLabel(tr("Общайтесь. Делитесь. Без лишнего."),
                                "EmptySubtitle", Qt::AlignCenter));

    // Переключатель режима.
    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(6);

    m_loginTab = new QPushButton(tr("Вход"), card);
    m_loginTab->setObjectName(QStringLiteral("TabButton"));
    m_loginTab->setCheckable(true);
    m_loginTab->setChecked(true);
    m_loginTab->setCursor(Qt::PointingHandCursor);

    m_registerTab = new QPushButton(tr("Регистрация"), card);
    m_registerTab->setObjectName(QStringLiteral("TabButton"));
    m_registerTab->setCheckable(true);
    m_registerTab->setCursor(Qt::PointingHandCursor);

    tabs->addWidget(m_loginTab, 1);
    tabs->addWidget(m_registerTab, 1);
    layout->addLayout(tabs);

    connect(m_loginTab, &QPushButton::clicked, this, [this]() {
        if (!m_registerMode)
            return;
        m_registerMode = false;
        applyMode();
    });
    connect(m_registerTab, &QPushButton::clicked, this, [this]() {
        if (m_registerMode)
            return;
        m_registerMode = true;
        applyMode();
    });

    m_username = new QLineEdit(card);
    m_username->setObjectName(QStringLiteral("LoginInput"));
    m_username->setPlaceholderText(tr("Логин"));
    layout->addWidget(m_username);

    m_displayName = new QLineEdit(card);
    m_displayName->setObjectName(QStringLiteral("LoginInput"));
    m_displayName->setPlaceholderText(tr("Как вас показывать (имя)"));
    layout->addWidget(m_displayName);

    m_password = new QLineEdit(card);
    m_password->setObjectName(QStringLiteral("LoginInput"));
    m_password->setPlaceholderText(tr("Пароль"));
    m_password->setEchoMode(QLineEdit::Password);
    layout->addWidget(m_password);

    m_remember = new QCheckBox(tr("Запомнить меня"), card);
    m_remember->setChecked(true);
    layout->addWidget(m_remember);

    // Сервер.
    auto *serverRow = new QHBoxLayout;
    serverRow->setSpacing(8);

    m_host = new QLineEdit(card);
    m_host->setObjectName(QStringLiteral("LoginInput"));
    m_host->setPlaceholderText(tr("Сервер"));
    m_host->setToolTip(tr("Адрес сервера"));

    m_port = new QLineEdit(card);
    m_port->setObjectName(QStringLiteral("LoginInput"));
    m_port->setValidator(new QIntValidator(1, 65535, m_port));
    m_port->setFixedWidth(84);
    m_port->setToolTip(tr("Порт"));

    serverRow->addWidget(m_host, 1);
    serverRow->addWidget(m_port);
    layout->addLayout(serverRow);

    auto *serverHint = makeLabel(tr("На другом компьютере укажите его IP и порт"),
                                 "EmptySubtitle", Qt::AlignLeft);
    serverHint->setWordWrap(true);
    layout->addWidget(serverHint);

    m_error = makeLabel(QString(), "ErrorLabel", Qt::AlignCenter);
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);

    m_submit = new QPushButton(tr("Войти"), card);
    m_submit->setObjectName(QStringLiteral("PrimaryButton"));
    m_submit->setCursor(Qt::PointingHandCursor);
    m_submit->setMinimumHeight(40);
    m_submit->setDefault(true);
    layout->addWidget(m_submit);

    m_status = makeLabel(tr("Подключение…"), "LoginStatus", Qt::AlignCenter);
    layout->addWidget(m_status);

    connect(m_submit, &QPushButton::clicked, this, &LoginWindow::onSubmit);
    connect(m_password, &QLineEdit::returnPressed, this, &LoginWindow::onSubmit);
    connect(m_username, &QLineEdit::returnPressed, this, &LoginWindow::onSubmit);

    center->addWidget(card, 0, Qt::AlignCenter);
    center->addStretch(1);
    outer->addLayout(center);

    outer->addStretch(1);
}

void LoginWindow::prepare()
{
    saveEndpoint();
    m_connection->setEndpoint(m_host->text().trimmed(),
                              static_cast<quint16>(m_port->text().toUShort()));
    if (!m_connection->isConnected())
        m_connection->connectToServer();
}

void LoginWindow::onModeChanged()
{
    applyMode();
}

void LoginWindow::applyMode()
{
    m_loginTab->setChecked(!m_registerMode);
    m_registerTab->setChecked(m_registerMode);

    m_displayName->setVisible(m_registerMode);
    m_remember->setVisible(!m_registerMode);
    m_submit->setText(m_registerMode ? tr("Создать аккаунт") : tr("Войти"));
    m_password->setPlaceholderText(m_registerMode ? tr("Пароль (от 8 символов)") : tr("Пароль"));

    hideError();
}

void LoginWindow::onSubmit()
{
    if (!ensureConnected())
        return;

    const QString username = m_username->text().trimmed();
    const QString password = m_password->text();

    if (username.isEmpty() || password.isEmpty()) {
        showError(tr("Введите логин и пароль."));
        return;
    }

    hideError();
    m_submit->setEnabled(false);
    m_submit->setText(tr("Подождите…"));

    if (m_registerMode) {
        const QString displayName = m_displayName->text().trimmed().isEmpty()
                                        ? username
                                        : m_displayName->text().trimmed();
        m_session->registerUser(username, displayName, password);
    } else {
        m_session->login(username, password, m_remember->isChecked());
    }
}

void LoginWindow::onAuthFailed(const QString &message)
{
    m_submit->setEnabled(true);
    applyMode();
    showError(message);
}

void LoginWindow::onConnectionStateChanged(net::ClientConnection::State state)
{
    switch (state) {
    case net::ClientConnection::State::Disconnected:
        m_status->setText(tr("Нет соединения с сервером…"));
        break;
    case net::ClientConnection::State::Connecting:
        m_status->setText(tr("Подключение…"));
        break;
    case net::ClientConnection::State::Connected:
        m_status->setText(tr("Сервер доступен"));
        break;
    }
}

void LoginWindow::showError(const QString &text)
{
    m_error->setText(text);
    m_error->setVisible(!text.isEmpty());

    if (m_submit->text() == tr("Подождите…"))
        m_submit->setText(m_registerMode ? tr("Создать аккаунт") : tr("Войти"));
    m_submit->setEnabled(true);
}

void LoginWindow::hideError()
{
    m_error->clear();
    m_error->hide();
}

bool LoginWindow::ensureConnected()
{
    if (m_connection->isConnected())
        return true;

    saveEndpoint();
    m_connection->setEndpoint(m_host->text().trimmed(),
                              static_cast<quint16>(m_port->text().toUShort()));
    m_connection->connectToServer();

    showError(tr("Нет соединения с сервером. Проверьте адрес и запустите сервер."));
    return false;
}

void LoginWindow::saveEndpoint()
{
    if (!m_settings)
        return;

    const QString host = m_host->text().trimmed();
    const uint port = m_port->text().toUInt();

    if (!host.isEmpty())
        m_settings->setServerHost(host);
    if (port > 0 && port <= 65535)
        m_settings->setServerPort(static_cast<quint16>(port));
    m_settings->sync();
}

} // namespace ui
