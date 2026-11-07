#pragma once

#include <QHash>
#include <QObject>
#include <QString>

namespace app {

class SettingsStore;

// Управление темой: подставляет токены в resources/qss/base.qss, ставит QSS и палитру.
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum class Theme {
        Light = 0,
        Dark = 1
    };
    Q_ENUM(Theme)

    explicit ThemeManager(SettingsStore *settings, QObject *parent = nullptr);

    Theme theme() const { return m_theme; }
    void setTheme(Theme theme);
    void toggle();
    void apply();

    // QSS с подставленными токенами; пустая строка, если ресурс недоступен.
    static QString styleSheetFor(Theme theme);

signals:
    void themeChanged(app::ThemeManager::Theme theme);

private:
    static QHash<QString, QString> tokensFor(Theme theme);
    static void applyPalette(Theme theme);

    SettingsStore *m_settings = nullptr;
    Theme m_theme = Theme::Light;
};

} // namespace app

Q_DECLARE_METATYPE(app::ThemeManager::Theme)
