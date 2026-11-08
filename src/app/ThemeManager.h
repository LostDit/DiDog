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
    ~ThemeManager() override;

    Theme theme() const { return m_theme; }
    void setTheme(Theme theme);
    void toggle();
    void apply();

    // Пакет оформления с сервера: токены накладываются поверх базовой темы.
    void applyCustom(const QString &id, const QHash<QString, QString> &tokens);
    void clearCustom();
    QString customThemeId() const { return m_customId; }

    // QSS с подставленными токенами; пустая строка, если ресурс недоступен.
    static QString styleSheetFor(Theme theme);

    // Цвет токена текущей темы — для делегатов, которые рисуют вручную.
    QColor color(const QString &token) const;

    // Активный экземпляр (для делегатов и вспомогательного кода).
    static ThemeManager *instance();

signals:
    void themeChanged(app::ThemeManager::Theme theme);

private:
    static QHash<QString, QString> tokensFor(Theme theme);
    static QString styleSheetFromTokens(const QHash<QString, QString> &tokens);
    static void applyPaletteForTokens(const QHash<QString, QString> &tokens);

    SettingsStore *m_settings = nullptr;
    Theme m_theme = Theme::Light;
    QString m_customId;
    QHash<QString, QString> m_customTokens;
};

} // namespace app

Q_DECLARE_METATYPE(app::ThemeManager::Theme)
