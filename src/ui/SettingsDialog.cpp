#include "ui/SettingsDialog.h"

#include "app/AppPaths.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
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
                               app::BillingService *billing, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_theme(theme)
    , m_billing(billing)
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

    layout->addWidget(sectionTitle(tr("Подписка"), this));

    m_planLabel = new QLabel(this);
    m_planLabel->setObjectName(QStringLiteral("EmptySubtitle"));
    m_planLabel->setWordWrap(true);
    layout->addWidget(m_planLabel);

    auto *promoRow = new QHBoxLayout;
    promoRow->setSpacing(8);
    m_promo = new QLineEdit(this);
    m_promo->setObjectName(QStringLiteral("LoginInput"));
    m_promo->setPlaceholderText(tr("Промокод"));
    auto *promoButton = new QPushButton(tr("Активировать"), this);
    promoButton->setObjectName(QStringLiteral("GhostButton"));
    promoButton->setCursor(Qt::PointingHandCursor);
    promoRow->addWidget(m_promo, 1);
    promoRow->addWidget(promoButton);
    layout->addLayout(promoRow);

    m_promoStatus = new QLabel(this);
    m_promoStatus->setObjectName(QStringLiteral("EmptySubtitle"));
    m_promoStatus->setWordWrap(true);
    m_promoStatus->setVisible(false);
    layout->addWidget(m_promoStatus);

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
    connect(promoButton, &QPushButton::clicked, this, &SettingsDialog::onPromoClicked);

    if (m_billing) {
        connect(m_billing, &app::BillingService::statusChanged,
                this, &SettingsDialog::updatePlanLabel);
        connect(m_billing, &app::BillingService::themesLoaded,
                this, &SettingsDialog::onThemesLoaded);
        connect(m_billing, &app::BillingService::themeReady,
                this, &SettingsDialog::onThemeReady);
        connect(m_billing, &app::BillingService::themeFailed,
                this, &SettingsDialog::onThemeFailed);
        connect(m_billing, &app::BillingService::promoActivated, this, [this]() {
            if (m_promoStatus) {
                m_promoStatus->setText(tr("Подписка активирована."));
                m_promoStatus->setVisible(true);
            }
            if (m_promo)
                m_promo->clear();
            updatePlanLabel();
            m_billing->loadThemes();
        });
        connect(m_billing, &app::BillingService::promoFailed, this,
                [this](const QString &message) {
                    if (m_promoStatus) {
                        m_promoStatus->setText(message);
                        m_promoStatus->setVisible(true);
                    }
                });

        updatePlanLabel();
        m_billing->refresh();
        m_billing->loadThemes();
    }
}

void SettingsDialog::updatePlanLabel()
{
    if (!m_planLabel)
        return;

    const QString plan = m_billing ? m_billing->plan() : QStringLiteral("free");
    const qint64 expiresAt = m_billing ? m_billing->expiresAt() : 0;

    if (plan == QLatin1String("free")) {
        m_planLabel->setText(tr("Бесплатный план: базовые темы. Промокод даёт подписку."));
        return;
    }

    const QString date = QDateTime::fromMSecsSinceEpoch(expiresAt)
                             .toString(QStringLiteral("dd.MM.yyyy"));
    m_planLabel->setText(tr("Подписка %1 действует до %2.").arg(plan, date));
}

void SettingsDialog::onPromoClicked()
{
    if (!m_billing || !m_promo)
        return;

    if (m_promoStatus) {
        m_promoStatus->setText(tr("Проверяем…"));
        m_promoStatus->setVisible(true);
    }

    m_billing->redeem(m_promo->text());
}

void SettingsDialog::onThemesLoaded(const QList<app::ThemeInfo> &themes)
{
    if (!m_themeBox)
        return;

    const QSignalBlocker blocker(m_themeBox);

    while (m_themeBox->count() > 2)
        m_themeBox->removeItem(m_themeBox->count() - 1);

    for (const app::ThemeInfo &info : themes) {
        const QString label = info.premium
                                  ? tr("%1 — по подписке").arg(info.name)
                                  : info.name;
        m_themeBox->addItem(label, QStringLiteral("pack:") + info.id);

        const int index = m_themeBox->count() - 1;
        const bool allowed = !info.premium || (m_billing && m_billing->hasPremium());
        if (!allowed) {
            m_themeBox->setItemData(index, QVariant(Qt::NoItemFlags), Qt::UserRole - 1);
            m_themeBox->setItemData(index, tr("Нужна подписка"), Qt::ToolTipRole);
        }
    }

    if (m_theme && !m_theme->customThemeId().isEmpty()) {
        const int index = m_themeBox->findData(QStringLiteral("pack:")
                                               + m_theme->customThemeId());
        if (index >= 0)
            m_themeBox->setCurrentIndex(index);
    }
}

void SettingsDialog::onThemeReady(const QString &id, const QHash<QString, QString> &tokens)
{
    if (!m_theme)
        return;

    m_theme->applyCustom(id, tokens);

    if (m_promoStatus) {
        m_promoStatus->setText(tr("Тема применена."));
        m_promoStatus->setVisible(true);
    }
}

void SettingsDialog::onThemeFailed(const QString &message)
{
    if (!m_promoStatus)
        return;

    m_promoStatus->setText(message);
    m_promoStatus->setVisible(true);
}

void SettingsDialog::onThemeChanged(int index)
{
    if (!m_theme || index < 0)
        return;

    const QVariant data = m_themeBox->itemData(index);
    if (data.type() == QVariant::String) {
        const QString value = data.toString();
        if (value.startsWith(QLatin1String("pack:")) && m_billing)
            m_billing->requestTheme(value.mid(5));
        return;
    }

    m_theme->setTheme(data.toInt() == 1 ? app::ThemeManager::Theme::Dark
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
