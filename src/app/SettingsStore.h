#pragma once

#include <QByteArray>
#include <QSet>
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

    // "auto" | "ru" | "en"
    QString language() const;
    void setLanguage(const QString &language);

    bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);

    bool tlsEnabled() const;
    void setTlsEnabled(bool enabled);

    // Отпечаток сертификата сервера для конкретного адреса (TOFU-закрепление).
    QString certificatePin(const QString &host, quint16 port) const;
    void setCertificatePin(const QString &host, quint16 port, const QString &fingerprint);

    // Удалённые пользователем чаты: скрыты в списке, пока не придёт новое сообщение.
    QSet<qint64> hiddenChats() const;
    void setHiddenChats(const QSet<qint64> &chatIds);

    // Сообщения, скрытые «у себя».
    QSet<qint64> hiddenMessages() const;
    void setHiddenMessages(const QSet<qint64> &messageIds);

    // Подписка и выбранный пакет оформления.
    QString subscriptionPlan() const;
    qint64 subscriptionExpiresAt() const;
    void setSubscription(const QString &plan, qint64 expiresAt);

    QString customThemeId() const;
    QString customThemeTokens() const;
    void setCustomTheme(const QString &id, const QString &tokensJson);

    // Голос: push-to-talk и выбранные устройства (пусто — системные по умолчанию).
    bool voicePushToTalk() const;
    void setVoicePushToTalk(bool enabled);
    QString voiceInputDevice() const;
    QString voiceOutputDevice() const;
    void setVoiceDevices(const QString &input, const QString &output);

    // Сворачивать окно в трей при закрытии (по умолчанию включено).
    bool closeToTray() const;
    void setCloseToTray(bool enabled);

    QString filePath() const;

private:
    QSettings m_settings;
};

} // namespace app
