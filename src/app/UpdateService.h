#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QFile;

namespace net {
class ClientConnection;
}

namespace app {

struct UpdateFileInfo {
    QString path;
    qint64 size = 0;
    QString sha256;
};

// Автообновление: манифест с сервера, пофайловая дельта, докачка и запуск updater.
class UpdateService : public QObject
{
    Q_OBJECT

public:
    UpdateService(net::ClientConnection *connection, QObject *parent = nullptr);
    ~UpdateService() override;

    void checkForUpdates();
    void downloadAndApply();

    bool isBusy() const { return m_busy; }
    bool hasUpdate() const { return !m_files.isEmpty(); }
    QString availableVersion() const { return m_version; }
    qint64 totalBytes() const { return m_totalBytes; }

    static QString appDirectory();

signals:
    void checkFinished(bool available, const QString &version, qint64 totalBytes);
    void progress(int percent, const QString &file);
    void downloaded();
    void failed(const QString &message);

private slots:
    void onChunk(quint32 transferId, quint32 sequence, const QByteArray &data);
    void onTransferFinished(quint32 transferId);

private:
    void startNextFile();
    void finishFile();
    bool launchUpdater(QString *error);

    net::ClientConnection *m_connection = nullptr;
    QList<UpdateFileInfo> m_files;
    QString m_version;
    qint64 m_totalBytes = 0;
    qint64 m_doneBytes = 0;

    int m_index = 0;
    quint32 m_transferId = 0;
    QFile *m_currentFile = nullptr;
    UpdateFileInfo m_currentInfo;
    QString m_tempRoot;
    bool m_busy = false;
    bool m_retried = false;
};

} // namespace app
