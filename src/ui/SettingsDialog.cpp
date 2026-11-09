#include "ui/SettingsDialog.h"

#include "app/AppPaths.h"
#include "ui/ButtonFx.h"
#include "app/SettingsStore.h"
#include "app/ThemeManager.h"
#include "app/UpdateService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QAudioDeviceInfo>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTreeWidget>
#include "ui/PanScrollArea.h"
#include <QPushButton>
#include <QTabWidget>
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
                               app::BillingService *billing, app::UpdateService *updater,
                               QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_theme(theme)
    , m_billing(billing)
    , m_updater(updater)
{
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(tr("Настройки"));
    setModal(true);
    setMinimumSize(640, 560);

    buildUi();
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    m_settingsSearch = new QLineEdit(this);
    m_settingsSearch->setObjectName(QStringLiteral("SettingsSearch"));
    m_settingsSearch->setPlaceholderText(tr("Поиск настроек"));
    m_settingsSearch->setClearButtonEnabled(true);
    root->addWidget(m_settingsSearch);

    m_settingsTree = new QTreeWidget(this);
    m_settingsTree->setObjectName(QStringLiteral("SettingsTree"));
    m_settingsTree->setHeaderHidden(true);
    m_settingsTree->setFixedWidth(210);
    m_settingsTree->setRootIsDecorated(true);
    m_settingsTree->setIndentation(14);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("SettingsTabs"));
    m_tabs->tabBar()->hide();

    auto *contentRow = new QHBoxLayout;
    contentRow->setSpacing(12);
    contentRow->addWidget(m_settingsTree, 0);
    contentRow->addWidget(m_tabs, 1);
    root->addLayout(contentRow, 1);

    const auto makePage = [this](const QString &title) {
        auto *page = new QWidget(this);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(14, 12, 14, 12);
        pageLayout->setSpacing(10);

        auto *scroll = new PanScrollArea(this);
        scroll->setWidget(page);
        m_tabs->addTab(scroll, title);
        return pageLayout;
    };

    connect(m_settingsSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!m_tabs || text.trimmed().isEmpty())
            return;

        const QString query = text.toLower();
        const QList<QStringList> keywords = {
            {QStringLiteral("тема"), QStringLiteral("язык"), QStringLiteral("уведом")}, // Основные
            {QStringLiteral("подписк"), QStringLiteral("промокод")},
            {QStringLiteral("голос"), QStringLiteral("микрофон"), QStringLiteral("push"),
             QStringLiteral("динамик")},
            {QStringLiteral("окно"), QStringLiteral("трей"), QStringLiteral("анимац")},
            {QStringLiteral("обновлен")},
            {QStringLiteral("сервер"), QStringLiteral("адрес"), QStringLiteral("данные"),
             QStringLiteral("папк")},
        };

        for (int i = 0; i < keywords.size() && i < m_tabs->count(); ++i) {
            for (const QString &word : keywords.at(i)) {
                if (word.contains(query) || query.contains(word)) {
                    m_tabs->setCurrentIndex(i);
                    for (int n = 0; n < m_settingsTree->topLevelItemCount(); ++n) {
                        if (m_settingsTree->topLevelItem(n)->data(0, Qt::UserRole).toInt() == i) {
                            m_settingsTree->setCurrentItem(m_settingsTree->topLevelItem(n));
                            break;
                        }
                    }
                    return;
                }
            }
        }
    });

    auto *layout = makePage(tr("Основные"));

    layout->addWidget(sectionTitle(tr("Внешний вид"), this));

    auto *appearance = new QFormLayout;
    appearance->setLabelAlignment(Qt::AlignLeft);
    appearance->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    appearance->setHorizontalSpacing(16);
    appearance->setVerticalSpacing(10);

    m_themeBox = new QComboBox(this);
    m_themeBox->setObjectName(QStringLiteral("ThemeCombo"));
    m_themeBox->addItem(tr("Светлая"), 0);
    m_themeBox->addItem(tr("Тёмная"), 1);
    m_themeBox->setCurrentIndex(m_theme
                                    && m_theme->theme() == app::ThemeManager::Theme::Dark ? 1 : 0);
    appearance->addRow(tr("Тема"), m_themeBox);

    m_languageBox = new QComboBox(this);
    m_languageBox->setObjectName(QStringLiteral("LanguageCombo"));
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
    m_notifications->setObjectName(QStringLiteral("NotificationsCheck"));
    m_notifications->setChecked(!m_settings || m_settings->notificationsEnabled());
    layout->addWidget(m_notifications);

    layout->addStretch(1);
    layout = makePage(tr("Подписка"));

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

    layout->addStretch(1);
    layout = makePage(tr("Голос"));

    layout->addWidget(sectionTitle(tr("Голос"), this));

    m_pttCheck = new QCheckBox(tr("Push-to-talk: говорить, удерживая кнопку"), this);
    m_pttCheck->setObjectName(QStringLiteral("PttCheck"));
    m_pttCheck->setChecked(m_settings->voicePushToTalk());
    connect(m_pttCheck, &QCheckBox::toggled, this,
            [this](bool enabled) { m_settings->setVoicePushToTalk(enabled); });
    layout->addWidget(m_pttCheck);

    auto *devicesRow = new QHBoxLayout;
    devicesRow->setSpacing(8);

    m_inputCombo = new QComboBox(this);
    m_inputCombo->setObjectName(QStringLiteral("InputDeviceCombo"));
    m_inputCombo->addItem(tr("Микрофон по умолчанию"), QString());
    for (const QAudioDeviceInfo &device : QAudioDeviceInfo::availableDevices(QAudio::AudioInput))
        m_inputCombo->addItem(device.deviceName(), device.deviceName());
    m_inputCombo->setCurrentIndex(
        qMax(0, m_inputCombo->findData(m_settings->voiceInputDevice())));
    devicesRow->addWidget(m_inputCombo, 1);

    m_outputCombo = new QComboBox(this);
    m_outputCombo->setObjectName(QStringLiteral("OutputDeviceCombo"));
    m_outputCombo->addItem(tr("Динамики по умолчанию"), QString());
    for (const QAudioDeviceInfo &device : QAudioDeviceInfo::availableDevices(QAudio::AudioOutput))
        m_outputCombo->addItem(device.deviceName(), device.deviceName());
    m_outputCombo->setCurrentIndex(
        qMax(0, m_outputCombo->findData(m_settings->voiceOutputDevice())));
    devicesRow->addWidget(m_outputCombo, 1);

    layout->addLayout(devicesRow);

    const auto saveDevices = [this]() {
        m_settings->setVoiceDevices(m_inputCombo->currentData().toString(),
                                    m_outputCombo->currentData().toString());
    };
    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, saveDevices);
    connect(m_outputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, saveDevices);

    layout->addStretch(1);
    layout = makePage(tr("Окно"));

    layout->addWidget(sectionTitle(tr("Окно"), this));

    m_trayCheck = new QCheckBox(tr("Сворачивать в трей при закрытии"), this);
    m_trayCheck->setObjectName(QStringLiteral("TrayCheck"));
    m_trayCheck->setChecked(m_settings->closeToTray());
    connect(m_trayCheck, &QCheckBox::toggled, this,
            [this](bool enabled) { m_settings->setCloseToTray(enabled); });
    layout->addWidget(m_trayCheck);

    m_animationsCheck = new QCheckBox(tr("Анимации интерфейса"), this);
    m_animationsCheck->setObjectName(QStringLiteral("AnimationsCheck"));
    m_animationsCheck->setChecked(m_settings->animationsEnabled());
    connect(m_animationsCheck, &QCheckBox::toggled, this,
            [this](bool enabled) { m_settings->setAnimationsEnabled(enabled); ui::ButtonFx::setEnabled(enabled); });
    layout->addWidget(m_animationsCheck);

    layout->addStretch(1);
    layout = makePage(tr("Обновления"));

    layout->addWidget(sectionTitle(tr("Обновления"), this));

    m_updateStatus = new QLabel(tr("Версия %1").arg(QStringLiteral(MESSENGER_VERSION)), this);
    m_updateStatus->setObjectName(QStringLiteral("EmptySubtitle"));
    m_updateStatus->setWordWrap(true);
    layout->addWidget(m_updateStatus);

    m_updateButton = new QPushButton(tr("Проверить обновления"), this);
    m_updateButton->setObjectName(QStringLiteral("GhostButton"));
    m_updateButton->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_updateButton);

    m_updateProgress = new QProgressBar(this);
    m_updateProgress->setRange(0, 100);
    m_updateProgress->setVisible(false);
    layout->addWidget(m_updateProgress);

    layout->addStretch(1);
    layout = makePage(tr("Данные"));

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

    auto addNode = [this](const QString &title, int page, QTreeWidgetItem *parent) {
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_settingsTree);
        item->setText(0, title);
        item->setData(0, Qt::UserRole, page);
        return item;
    };

    auto *general = addNode(tr("Основные"), 0, nullptr);
    addNode(tr("Внешний вид"), 0, general);
    addNode(tr("Уведомления"), 0, general);
    addNode(tr("Окно"), 3, general);
    auto *subscription = addNode(tr("Подписка"), 1, nullptr);
    addNode(tr("План"), 1, subscription);
    addNode(tr("Промокод"), 1, subscription);
    auto *voiceNode = addNode(tr("Голос"), 2, nullptr);
    addNode(tr("Push-to-talk"), 2, voiceNode);
    addNode(tr("Устройства"), 2, voiceNode);
    auto *updatesNode = addNode(tr("Обновления"), 4, nullptr);
    addNode(tr("Проверка"), 4, updatesNode);
    auto *dataNode = addNode(tr("Данные"), 5, nullptr);
    addNode(tr("Сервер"), 5, dataNode);
    addNode(tr("Папка данных"), 5, dataNode);
    m_settingsTree->expandAll();

    connect(m_settingsTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
                if (!current)
                    return;
                const int page = current->data(0, Qt::UserRole).toInt();
                if (page >= 0 && page < m_tabs->count())
                    m_tabs->setCurrentIndex(page);
            });
    m_settingsTree->setCurrentItem(general);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("Закрыть"));
    root->addWidget(buttons);

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
    connect(m_updateButton, &QPushButton::clicked, this, &SettingsDialog::onCheckUpdatesClicked);

    if (m_updater) {
        connect(m_updater, &app::UpdateService::checkFinished,
                this, &SettingsDialog::onUpdateCheckFinished);
        connect(m_updater, &app::UpdateService::progress,
                this, &SettingsDialog::onUpdateProgress);
        connect(m_updater, &app::UpdateService::failed,
                this, &SettingsDialog::onUpdateFailed);
    }

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

void SettingsDialog::onCheckUpdatesClicked()
{
    if (!m_updater)
        return;

    if (m_updateReady) {
        m_updateButton->setEnabled(false);
        m_updateButton->setText(tr("Загрузка…"));
        m_updateStatus->setText(tr("Скачиваем изменённые файлы…"));
        m_updateProgress->setVisible(true);
        m_updateProgress->setValue(0);
        m_updater->downloadAndApply();
        return;
    }

    m_updateStatus->setText(tr("Проверяем обновления…"));
    m_updateButton->setEnabled(false);
    m_updater->checkForUpdates();
}

void SettingsDialog::onUpdateCheckFinished(bool available, const QString &version,
                                           qint64 totalBytes)
{
    m_updateButton->setEnabled(true);

    if (!available) {
        m_updateReady = false;
        m_updateButton->setText(tr("Проверить обновления"));
        m_updateStatus->setText(tr("Установлена последняя версия (%1).")
                                    .arg(QStringLiteral(MESSENGER_VERSION)));
        m_updateProgress->setVisible(false);
        return;
    }

    m_updateReady = true;
    m_updateButton->setText(tr("Установить обновление"));
    m_updateStatus->setText(tr("Доступна версия %1 — %2 МБ. Скачаются только изменённые файлы.")
                                .arg(version)
                                .arg(totalBytes / (1024.0 * 1024.0), 0, 'f', 1));
}

void SettingsDialog::onUpdateProgress(int percent, const QString &file)
{
    m_updateProgress->setVisible(true);
    m_updateProgress->setValue(percent);

    if (!file.isEmpty())
        m_updateStatus->setText(tr("Загрузка: %1 (%2%)").arg(file).arg(percent));
}

void SettingsDialog::onUpdateFailed(const QString &message)
{
    m_updateButton->setEnabled(true);
    m_updateButton->setText(m_updateReady ? tr("Установить обновление")
                                          : tr("Проверить обновления"));
    m_updateStatus->setText(message);
    m_updateProgress->setVisible(false);
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
