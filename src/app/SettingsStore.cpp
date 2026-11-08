#include "app/SettingsStore.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace app {
namespace {

// Сервер по умолчанию: messenger.ini рядом с исполняемым файлом.
// Позволяет собрать клиент, который сам подключается к нужному серверу.
QVariant defaultNetworkValue(const QString &key, const QVariant &fallback = QVariant())
{
    QSettings ini(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/messenger.ini"),
                  QSettings::IniFormat);
    ini.setIniCodec("UTF-8");
    return ini.value(key, fallback);
}

QString defaultServerHost()
{
    return defaultNetworkValue(QStringLiteral("network/host")).toString();
}

quint16 defaultServerPort()
{
    return static_cast<quint16>(
        defaultNetworkValue(QStringLiteral("network/port"), 45000).toUInt());
}

bool defaultTlsEnabled()
{
    return defaultNetworkValue(QStringLiteral("network/tls"), false).toBool();
}

} // namespace

namespace {

constexpr auto kThemeKey = "ui/theme";
constexpr auto kGeometryKey = "ui/geometry";
constexpr auto kWindowStateKey = "ui/windowState";
constexpr auto kServerHostKey = "network/host";
constexpr auto kServerPortKey = "network/port";
constexpr auto kAuthTokenKey = "auth/token";
constexpr auto kLastUserKey = "auth/lastUser";
constexpr auto kLanguageKey = "ui/language";
constexpr auto kNotificationsKey = "ui/notifications";
constexpr auto kTlsKey = "network/tls";

} // namespace

SettingsStore::SettingsStore(const QString &filePath)
    : m_settings(filePath, QSettings::IniFormat)
{
    m_settings.setIniCodec("UTF-8");
}

QVariant SettingsStore::value(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(key, fallback);
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}

void SettingsStore::sync()
{
    m_settings.sync();
}

int SettingsStore::theme() const
{
    return m_settings.value(kThemeKey, 0).toInt();
}

void SettingsStore::setTheme(int theme)
{
    m_settings.setValue(kThemeKey, theme);
}

QByteArray SettingsStore::windowGeometry() const
{
    return m_settings.value(kGeometryKey).toByteArray();
}

void SettingsStore::setWindowGeometry(const QByteArray &geometry)
{
    m_settings.setValue(kGeometryKey, geometry);
}

QByteArray SettingsStore::windowState() const
{
    return m_settings.value(kWindowStateKey).toByteArray();
}

void SettingsStore::setWindowState(const QByteArray &state)
{
    m_settings.setValue(kWindowStateKey, state);
}

QString SettingsStore::serverHost() const
{
    const QString saved = m_settings.value(kServerHostKey).toString();
    if (!saved.isEmpty())
        return saved;

    const QString fallback = defaultServerHost();
    return fallback.isEmpty() ? QStringLiteral("127.0.0.1") : fallback;
}

void SettingsStore::setServerHost(const QString &host)
{
    m_settings.setValue(kServerHostKey, host);
}

quint16 SettingsStore::serverPort() const
{
    if (m_settings.contains(kServerPortKey))
        return static_cast<quint16>(m_settings.value(kServerPortKey).toUInt());

    return defaultServerPort();
}

void SettingsStore::setServerPort(quint16 port)
{
    m_settings.setValue(kServerPortKey, static_cast<uint>(port));
}

QString SettingsStore::authToken() const
{
    return m_settings.value(kAuthTokenKey).toString();
}

void SettingsStore::setAuthToken(const QString &token)
{
    if (token.isEmpty())
        m_settings.remove(kAuthTokenKey);
    else
        m_settings.setValue(kAuthTokenKey, token);
}

QString SettingsStore::lastUserName() const
{
    return m_settings.value(kLastUserKey).toString();
}

void SettingsStore::setLastUserName(const QString &name)
{
    m_settings.setValue(kLastUserKey, name);
}

QString SettingsStore::language() const
{
    return m_settings.value(kLanguageKey, QStringLiteral("auto")).toString();
}

void SettingsStore::setLanguage(const QString &language)
{
    m_settings.setValue(kLanguageKey, language);
}

bool SettingsStore::notificationsEnabled() const
{
    return m_settings.value(kNotificationsKey, true).toBool();
}

void SettingsStore::setNotificationsEnabled(bool enabled)
{
    m_settings.setValue(kNotificationsKey, enabled);
}

bool SettingsStore::tlsEnabled() const
{
    if (m_settings.contains(kTlsKey))
        return m_settings.value(kTlsKey).toBool();

    return defaultTlsEnabled();
}

void SettingsStore::setTlsEnabled(bool enabled)
{
    m_settings.setValue(kTlsKey, enabled);
}

QString SettingsStore::certificatePin(const QString &host, quint16 port) const
{
    return m_settings.value(QStringLiteral("tls/pin/%1_%2").arg(host).arg(port)).toString();
}

void SettingsStore::setCertificatePin(const QString &host, quint16 port,
                                      const QString &fingerprint)
{
    const QString key = QStringLiteral("tls/pin/%1_%2").arg(host).arg(port);

    if (fingerprint.isEmpty())
        m_settings.remove(key);
    else
        m_settings.setValue(key, fingerprint);
}

QSet<qint64> SettingsStore::hiddenChats() const
{
    QSet<qint64> result;

    const QString stored = m_settings.value(QStringLiteral("chats/hidden")).toString();
    const QStringList parts = stored.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const qint64 id = part.trimmed().toLongLong(&ok);
        if (ok && id > 0)
            result.insert(id);
    }

    return result;
}

void SettingsStore::setHiddenChats(const QSet<qint64> &chatIds)
{
    QList<qint64> sorted = chatIds.values();
    std::sort(sorted.begin(), sorted.end());

    QStringList parts;
    for (const qint64 id : sorted)
        parts.append(QString::number(id));

    if (parts.isEmpty())
        m_settings.remove(QStringLiteral("chats/hidden"));
    else
        m_settings.setValue(QStringLiteral("chats/hidden"), parts.join(QLatin1Char(',')));
}

QSet<qint64> SettingsStore::hiddenMessages() const
{
    QSet<qint64> result;

    const QString stored = m_settings.value(QStringLiteral("chats/hiddenMessages")).toString();
    const QStringList parts = stored.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const qint64 id = part.trimmed().toLongLong(&ok);
        if (ok && id > 0)
            result.insert(id);
    }

    return result;
}

void SettingsStore::setHiddenMessages(const QSet<qint64> &messageIds)
{
    QList<qint64> sorted = messageIds.values();
    std::sort(sorted.begin(), sorted.end());

    QStringList parts;
    for (const qint64 id : sorted)
        parts.append(QString::number(id));

    if (parts.isEmpty())
        m_settings.remove(QStringLiteral("chats/hiddenMessages"));
    else
        m_settings.setValue(QStringLiteral("chats/hiddenMessages"), parts.join(QLatin1Char(',')));
}

QString SettingsStore::customThemeId() const
{
    return m_settings.value(QStringLiteral("ui/customTheme")).toString();
}

QString SettingsStore::customThemeTokens() const
{
    return m_settings.value(QStringLiteral("ui/customThemeTokens")).toString();
}

void SettingsStore::setCustomTheme(const QString &id, const QString &tokensJson)
{
    if (id.isEmpty())
        m_settings.remove(QStringLiteral("ui/customTheme"));
    else
        m_settings.setValue(QStringLiteral("ui/customTheme"), id);

    if (tokensJson.isEmpty())
        m_settings.remove(QStringLiteral("ui/customThemeTokens"));
    else
        m_settings.setValue(QStringLiteral("ui/customThemeTokens"), tokensJson);
}

QString SettingsStore::subscriptionPlan() const
{
    return m_settings.value(QStringLiteral("billing/plan"), QStringLiteral("free")).toString();
}

qint64 SettingsStore::subscriptionExpiresAt() const
{
    return m_settings.value(QStringLiteral("billing/expires")).toLongLong();
}

void SettingsStore::setSubscription(const QString &plan, qint64 expiresAt)
{
    m_settings.setValue(QStringLiteral("billing/plan"), plan);
    m_settings.setValue(QStringLiteral("billing/expires"), expiresAt);
}

bool SettingsStore::voicePushToTalk() const
{
    return m_settings.value(QStringLiteral("voice/pushToTalk"), false).toBool();
}

void SettingsStore::setVoicePushToTalk(bool enabled)
{
    m_settings.setValue(QStringLiteral("voice/pushToTalk"), enabled);
}

QString SettingsStore::voiceInputDevice() const
{
    return m_settings.value(QStringLiteral("voice/inputDevice")).toString();
}

QString SettingsStore::voiceOutputDevice() const
{
    return m_settings.value(QStringLiteral("voice/outputDevice")).toString();
}

void SettingsStore::setVoiceDevices(const QString &input, const QString &output)
{
    m_settings.setValue(QStringLiteral("voice/inputDevice"), input);
    m_settings.setValue(QStringLiteral("voice/outputDevice"), output);
}

bool SettingsStore::closeToTray() const
{
    return m_settings.value(QStringLiteral("window/closeToTray"), true).toBool();
}

void SettingsStore::setCloseToTray(bool enabled)
{
    m_settings.setValue(QStringLiteral("window/closeToTray"), enabled);
}

bool SettingsStore::animationsEnabled() const
{
    return m_settings.value(QStringLiteral("ui/animations"), true).toBool();
}

void SettingsStore::setAnimationsEnabled(bool enabled)
{
    m_settings.setValue(QStringLiteral("ui/animations"), enabled);
}

QString SettingsStore::filePath() const
{
    return m_settings.fileName();
}

} // namespace app
