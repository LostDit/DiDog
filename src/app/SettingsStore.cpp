#include "app/SettingsStore.h"

namespace app {
namespace {

constexpr auto kThemeKey = "ui/theme";
constexpr auto kGeometryKey = "ui/geometry";
constexpr auto kWindowStateKey = "ui/windowState";
constexpr auto kServerHostKey = "network/host";
constexpr auto kServerPortKey = "network/port";
constexpr auto kAuthTokenKey = "auth/token";
constexpr auto kLastUserKey = "auth/lastUser";

constexpr quint16 kDefaultPort = 45000;

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
    return m_settings.value(kServerHostKey, QStringLiteral("127.0.0.1")).toString();
}

void SettingsStore::setServerHost(const QString &host)
{
    m_settings.setValue(kServerHostKey, host);
}

quint16 SettingsStore::serverPort() const
{
    return static_cast<quint16>(m_settings.value(kServerPortKey, kDefaultPort).toUInt());
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

QString SettingsStore::filePath() const
{
    return m_settings.fileName();
}

} // namespace app
