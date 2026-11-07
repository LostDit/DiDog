#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace app {
class SettingsStore;
class ThemeManager;
} // namespace app

namespace ui {

// Настройки: тема, язык, уведомления, адрес сервера, папка данных.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(app::SettingsStore *settings, app::ThemeManager *theme,
                   QWidget *parent = nullptr);

private slots:
    void onThemeChanged(int index);
    void onLanguageChanged(int index);
    void onNotificationsToggled(bool enabled);
    void onServerEdited();
    void openDataFolder();

private:
    void buildUi();

    app::SettingsStore *m_settings = nullptr;
    app::ThemeManager *m_theme = nullptr;

    QComboBox *m_themeBox = nullptr;
    QComboBox *m_languageBox = nullptr;
    QLabel *m_languageHint = nullptr;
    QCheckBox *m_notifications = nullptr;
    QLineEdit *m_host = nullptr;
    QLineEdit *m_port = nullptr;
};

} // namespace ui
