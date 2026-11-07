#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QVariant>

namespace app {

// Настройки приложения в INI-файле каталога данных (свой у каждого профиля).
// Все ключи — в группе по смыслу, никакой магии с QSettings по умолчанию.
class SettingsStore
{
public:
    explicit SettingsStore(const QString &filePath);

    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
    void sync();

    // 0 — светлая, 1 — тёмная.
    int theme() const;
    void setTheme(int theme);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    QByteArray windowState() const;
    void setWindowState(const QByteArray &state);

    QString serverHost() const;
    void setServerHost(const QString &host);
    quint16 serverPort() const;
    void setServerPort(quint16 port);

    QString authToken() const;
    void setAuthToken(const QString &token);
    QString lastUserName() const;
    void setLastUserName(const QString &name);

    QString filePath() const;

private:
    QSettings m_settings;
};

} // namespace app
