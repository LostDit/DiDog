#pragma once

#include "app/BillingService.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace app {
class BillingService;
class SettingsStore;
class ThemeManager;
class UpdateService;
} // namespace app

namespace ui {

// Настройки: тема, язык, уведомления, адрес сервера, папка данных, подписка.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(app::SettingsStore *settings, app::ThemeManager *theme,
                   app::BillingService *billing = nullptr,
                   app::UpdateService *updater = nullptr, QWidget *parent = nullptr);

private slots:
    void onThemeChanged(int index);
    void onLanguageChanged(int index);
    void onNotificationsToggled(bool enabled);
    void onServerEdited();
    void openDataFolder();

    void onPromoClicked();
    void onThemesLoaded(const QList<app::ThemeInfo> &themes);
    void onThemeReady(const QString &id, const QHash<QString, QString> &tokens);
    void onThemeFailed(const QString &message);
    void updatePlanLabel();
    void onCheckUpdatesClicked();
    void onUpdateCheckFinished(bool available, const QString &version, qint64 totalBytes);
    void onUpdateProgress(int percent, const QString &file);
    void onUpdateFailed(const QString &message);

private:
    void buildUi();

    app::SettingsStore *m_settings = nullptr;
    app::ThemeManager *m_theme = nullptr;
    app::BillingService *m_billing = nullptr;

    QComboBox *m_themeBox = nullptr;
    QComboBox *m_languageBox = nullptr;
    QLabel *m_languageHint = nullptr;
    QCheckBox *m_notifications = nullptr;
    QLineEdit *m_host = nullptr;
    QLineEdit *m_port = nullptr;

    QLabel *m_planLabel = nullptr;
    QLineEdit *m_promo = nullptr;
    QLabel *m_promoStatus = nullptr;

    app::UpdateService *m_updater = nullptr;
    QLabel *m_updateStatus = nullptr;
    QPushButton *m_updateButton = nullptr;
    QProgressBar *m_updateProgress = nullptr;
    bool m_updateReady = false;
};

} // namespace ui
