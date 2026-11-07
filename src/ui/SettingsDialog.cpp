#include "ui/SettingsDialog.h"

#include "app/AppPaths.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace ui {
namespace {

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("SectionTitle"));
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(app::SettingsStore *settings, app::ThemeManager *theme,
                               QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_theme(theme)
{
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(tr("Настройки"));
    setModal(true);
    setMinimumWidth(460);

    buildUi();
}

void SettingsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    layout->addWidget(sectionTitle(tr("Внешний вид"), this));

    auto *appearance = new QFormLayout;
    appearance->setLabelAlignment(Qt::AlignLeft);
    appearance->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    appearance->setHorizontalSpacing(16);
    appearance->setVerticalSpacing(10);

    m_themeBox = new QComboBox(this);
    m_themeBox->setObjectName(QStringLiteral("SettingsCombo"));
    m_themeBox->addItem(tr("Светлая"), 0);
    m_themeBox->addItem(tr("Тёмная"), 1);
    m_themeBox->setCurrentIndex(m_theme
                                    && m_theme->theme() == app::ThemeManager::Theme::Dark ? 1 : 0);
    appearance->addRow(tr("Тема"), m_themeBox);

    m_languageBox = new QComboBox(this);
    m_languageBox->setObjectName(QStringLiteral("SettingsCombo"));
    m_languageBox->addItem(tr("Системный"), QStringLiteral("auto"));
    m_languageBox->addItem(tr("Русский"), QStringLiteral("ru"));
    m_languageBox->addItem(QStringLiteral("English"), QStringLiteral("en"));
    if (m_settings) {
        const int index = m_languageBox->findData(m_settings->language());
        m_languageBox->setCurrentIndex(index >= 0 ? index : 0);
    }
    appearance->addRow(tr("Язык"), m_languageBox);

    m_languageHint = new QLabel(tr("Язык применится после перезапуска приложения."), this);
    m_languageHint->setObjectName(QStringLiteral("EmptySubtitle"));
    m_languageHint->setVisible(false);
    layout->addLayout(appearance);
    layout->addWidget(m_languageHint);

    layout->addWidget(sectionTitle(tr("Уведомления"), this));

    m_notifications = new QCheckBox(tr("Показывать уведомления о новых сообщениях"), this);
    m_notifications->setChecked(!m_settings || m_settings->notificationsEnabled());
    layout->addWidget(m_notifications);

    layout->addWidget(sectionTitle(tr("Сервер"), this));

    auto *serverRow = new QHBoxLayout;
    serverRow->setSpacing(8);

    m_host = new QLineEdit(this);
    m_host->setObjectName(QStringLiteral("LoginInput"));
    m_host->setPlaceholderText(tr("Адрес"));
    m_port = new QLineEdit(this);
    m_port->setObjectName(QStringLiteral("LoginInput"));
    m_port->setValidator(new QIntValidator(1, 65535, m_port));
    m_port->setFixedWidth(84);
    m_port->setPlaceholderText(tr("Порт"));

    if (m_settings) {
        m_host->setText(m_settings->serverHost());
        m_port->setText(QString::number(m_settings->serverPort()));
    }

    serverRow->addWidget(m_host, 1);
    serverRow->addWidget(m_port);
    layout->addLayout(serverRow);

    auto *serverHint = new QLabel(tr("Новый адрес применится при следующем входе."), this);
    serverHint->setObjectName(QStringLiteral("EmptySubtitle"));
    layout->addWidget(serverHint);

    layout->addWidget(sectionTitle(tr("Данные"), this));

    auto *dataRow = new QHBoxLayout;
    dataRow->setSpacing(8);

    auto *pathLabel = new QLabel(app::paths::dataDir(), this);
    pathLabel->setObjectName(QStringLiteral("EmptySubtitle"));
    pathLabel->setWordWrap(true);

    auto *openButton = new QPushButton(tr("Открыть папку"), this);
    openButton->setObjectName(QStringLiteral("GhostButton"));
    openButton->setCursor(Qt::PointingHandCursor);

    dataRow->addWidget(pathLabel, 1);
    dataRow->addWidget(openButton, 0, Qt::AlignTop);
    layout->addLayout(dataRow);

    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Закрыть"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(m_themeBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onThemeChanged);
    connect(m_languageBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onLanguageChanged);
    connect(m_notifications, &QCheckBox::toggled, this, &SettingsDialog::onNotificationsToggled);
    connect(m_host, &QLineEdit::editingFinished, this, &SettingsDialog::onServerEdited);
    connect(m_port, &QLineEdit::editingFinished, this, &SettingsDialog::onServerEdited);
    connect(openButton, &QPushButton::clicked, this, &SettingsDialog::openDataFolder);
}

void SettingsDialog::onThemeChanged(int index)
{
    if (!m_theme)
        return;

    m_theme->setTheme(index == 1 ? app::ThemeManager::Theme::Dark
                                 : app::ThemeManager::Theme::Light);
}

void SettingsDialog::onLanguageChanged(int index)
{
    const QString language = m_languageBox->itemData(index).toString();

    if (m_settings) {
        m_settings->setLanguage(language);
        m_settings->sync();
    }

    m_languageHint->setVisible(true);
}

void SettingsDialog::onNotificationsToggled(bool enabled)
{
    if (!m_settings)
        return;

    m_settings->setNotificationsEnabled(enabled);
    m_settings->sync();
}

void SettingsDialog::onServerEdited()
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

void SettingsDialog::openDataFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(app::paths::dataDir()));
}

} // namespace ui
