#include "app/ThemeManager.h"

#include "app/SettingsStore.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QPointer>
#include <QTextStream>

namespace app {
namespace {

const char kStyleSheetPath[] = ":/qss/base.qss";

struct TokenPair {
    const char *name;
    const char *light;
    const char *dark;
};

// Единственный источник палитры: правки здесь сразу видны во всём интерфейсе.
const TokenPair kTokens[] = {
    {"bg",             "#F5F6F8", "#14161A"},
    {"surface",        "#FFFFFF", "#1C1F24"},
    {"surface-2",      "#EEF0F3", "#24272E"},
    {"surface-3",      "#E3E7ED", "#2C3038"},
    {"border",         "#E3E6EA", "#2E3238"},
    {"border-strong",  "#D5D9DF", "#3A3F47"},
    {"text",           "#191C21", "#E8EAED"},
    {"text-muted",     "#6B7280", "#9AA0A6"},
    {"text-faint",     "#9CA3AF", "#6B7280"},
    {"hover",          "#F0F2F5", "#262A31"},
    {"selected",       "#E8F0FE", "#22304D"},
    {"accent",         "#3B7DF6", "#5B8CFF"},
    {"accent-hover",   "#2F6BDB", "#6E9BFF"},
    {"accent-pressed", "#285DC0", "#4F7BE0"},
    {"accent-soft",    "#E8F0FE", "#22304D"},
    {"scroll",         "#C7CBD1", "#3A3F47"},
    {"scroll-hover",   "#AEB4BC", "#4A505A"},
    {"danger",         "#EF4444", "#F87171"},
    {"warning",        "#D97706", "#FBBF24"},
    {"success",        "#22C55E", "#34D399"},
};

} // namespace

QColor ThemeManager::color(const QString &token) const
{
    QHash<QString, QString> tokens = tokensFor(m_theme);
    for (auto it = m_customTokens.constBegin(); it != m_customTokens.constEnd(); ++it)
        tokens.insert(it.key(), it.value());

    const QString value = tokens.value(token);
    return value.isEmpty() ? QColor() : QColor(value);
}

namespace {
QPointer<ThemeManager> g_instance;
}

ThemeManager *ThemeManager::instance()
{
    return g_instance.data();
}

ThemeManager::ThemeManager(SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    g_instance = this;

    if (m_settings) {
        const int stored = m_settings->theme();
        m_theme = (stored == static_cast<int>(Theme::Dark)) ? Theme::Dark : Theme::Light;

        m_customId = m_settings->customThemeId();
        if (!m_customId.isEmpty()) {
            const QJsonObject object =
                QJsonDocument::fromJson(m_settings->customThemeTokens().toUtf8()).object();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it)
                m_customTokens.insert(it.key(), it.value().toString());
        }
    }
}

ThemeManager::~ThemeManager()
{
    if (g_instance == this)
        g_instance.clear();
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_theme == theme && m_customId.isEmpty())
        return;

    m_theme = theme;
    m_customId.clear();
    m_customTokens.clear();

    if (m_settings) {
        m_settings->setTheme(static_cast<int>(theme));
        m_settings->setCustomTheme(QString(), QString());
        m_settings->sync();
    }

    apply();
    emit themeChanged(m_theme);
}

void ThemeManager::toggle()
{
    setTheme(m_theme == Theme::Light ? Theme::Dark : Theme::Light);
}

void ThemeManager::applyCustom(const QString &id, const QHash<QString, QString> &tokens)
{
    m_customId = id;
    m_customTokens = tokens;

    if (m_settings) {
        QJsonObject object;
        for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it)
            object.insert(it.key(), it.value());
        m_settings->setCustomTheme(
            id, QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
        m_settings->sync();
    }

    apply();
    emit themeChanged(m_theme);
}

void ThemeManager::clearCustom()
{
    m_customId.clear();
    m_customTokens.clear();

    if (m_settings) {
        m_settings->setCustomTheme(QString(), QString());
        m_settings->sync();
    }

    apply();
    emit themeChanged(m_theme);
}

void ThemeManager::apply()
{
    QHash<QString, QString> tokens = tokensFor(m_theme);
    for (auto it = m_customTokens.constBegin(); it != m_customTokens.constEnd(); ++it)
        tokens.insert(it.key(), it.value());

    applyPaletteForTokens(tokens);

    if (auto *application = qobject_cast<QApplication *>(QCoreApplication::instance()))
        application->setStyleSheet(styleSheetFromTokens(tokens));
}

QString ThemeManager::styleSheetFor(Theme theme)
{
    return styleSheetFromTokens(tokensFor(theme));
}

QString ThemeManager::styleSheetFromTokens(const QHash<QString, QString> &tokens)
{
    QFile file(QString::fromLatin1(kStyleSheetPath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Не удалось открыть QSS" << kStyleSheetPath;
        return QString();
    }

    QString styleSheet = QString::fromUtf8(file.readAll());
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it)
        styleSheet.replace(QLatin1String("{{") + it.key() + QLatin1String("}}"), it.value());

    return styleSheet;
}

QHash<QString, QString> ThemeManager::tokensFor(Theme theme)
{
    QHash<QString, QString> tokens;
    tokens.reserve(static_cast<int>(sizeof(kTokens) / sizeof(kTokens[0])));

    for (const TokenPair &token : kTokens) {
        const bool dark = (theme == Theme::Dark);
        tokens.insert(QString::fromLatin1(token.name),
                      QString::fromLatin1(dark ? token.dark : token.light));
    }
    return tokens;
}

void ThemeManager::applyPaletteForTokens(const QHash<QString, QString> &tokens)
{
    const auto colorOf = [&tokens](const char *name, const char *fallback) {
        const QString value = tokens.value(QString::fromLatin1(name));
        return QColor(value.isEmpty() ? QString::fromLatin1(fallback) : value);
    };

    const QColor window = colorOf("bg", "#F5F6F8");
    const QColor base = colorOf("surface", "#FFFFFF");
    const QColor text = colorOf("text", "#191C21");
    const QColor muted = colorOf("text-muted", "#6B7280");
    const QColor accent = colorOf("accent", "#3B7DF6");
    const QColor button = colorOf("surface-2", "#EEF0F3");

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, button);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);

    QApplication::setPalette(palette);
}

} // namespace app
