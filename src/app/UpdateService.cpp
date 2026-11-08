#include "app/UpdateService.h"

#include "core/Protocol.h"
#include "net/ClientConnection.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>

namespace app {
namespace {

QString sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();

    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

UpdateService::UpdateService(net::ClientConnection *connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
    if (!m_connection)
        return;

    connect(m_connection, &net::ClientConnection::chunkReceived,
            this, &UpdateService::onChunk);
    connect(m_connection, &net::ClientConnection::transferFinished,
            this, &UpdateService::onTransferFinished);
}

UpdateService::~UpdateService()
{
    if (m_currentFile) {
        m_currentFile->close();
        delete m_currentFile;
    }
}

QString UpdateService::appDirectory()
{
    return QCoreApplication::applicationDirPath();
}

void UpdateService::checkForUpdates()
{
    if (!m_connection || !m_connection->isConnected()) {
        emit failed(tr("Нет соединения с сервером."));
        return;
    }

    m_busy = true;
    m_connection->request(
        QStringLiteral("update.manifest"), QJsonObject(),
        [this](const QJsonObject &reply) {
            m_busy = false;

            if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                emit failed(core::proto::errorMessageOf(reply));
                return;
            }

            const QJsonObject data = reply.value(QStringLiteral("data")).toObject();
            const QString version = data.value(QStringLiteral("version")).toString();
            const QJsonArray array = data.value(QStringLiteral("files")).toArray();

            if (version == QStringLiteral(MESSENGER_VERSION)) {
                m_files.clear();
                emit checkFinished(false, version, 0);
                return;
            }

            const QString root = appDirectory();
            QList<UpdateFileInfo> changed;
            qint64 total = 0;

            for (const QJsonValue &value : array) {
                const QJsonObject item = value.toObject();
                UpdateFileInfo info;
                info.path = item.value(QStringLiteral("path")).toString();
                info.size = static_cast<qint64>(item.value(QStringLiteral("size")).toDouble());
                info.sha256 = item.value(QStringLiteral("sha256")).toString();

                const QString local = root + QLatin1Char('/') + info.path;
                const QFileInfo localInfo(local);
                if (localInfo.exists() && localInfo.size() == info.size
                    && sha256OfFile(local).compare(info.sha256, Qt::CaseInsensitive) == 0) {
                    continue;
                }

                changed.append(info);
                total += info.size;
            }

            m_version = version;
            m_files = changed;
            m_totalBytes = total;
            m_doneBytes = 0;
            m_index = 0;

            emit checkFinished(!changed.isEmpty(), version, total);
        },
        15000);
}

void UpdateService::downloadAndApply()
{
    if (m_files.isEmpty()) {
        emit failed(tr("Нет доступных обновлений."));
        return;
    }
    if (!m_connection || !m_connection->isConnected()) {
        emit failed(tr("Нет соединения с сервером."));
        return;
    }

    m_tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                 + QStringLiteral("/MessengerUpdate/") + m_version;
    if (!QDir().mkpath(m_tempRoot)) {
        emit failed(tr("Не удалось создать каталог обновления."));
        return;
    }

    m_busy = true;
    m_index = 0;
    m_doneBytes = 0;
    startNextFile();
}

void UpdateService::startNextFile()
{
    if (m_index >= m_files.size()) {
        m_busy = false;
        emit progress(100, QString());

        QString error;
        if (!launchUpdater(&error))
            emit failed(error);
        return;
    }

    m_currentInfo = m_files.at(m_index);
    m_retried = false;

    const QString part = m_tempRoot + QLatin1Char('/') + m_currentInfo.path
                         + QStringLiteral(".part");
    QDir().mkpath(QFileInfo(part).absolutePath());

    m_currentFile = new QFile(part);
    if (!m_currentFile->open(QIODevice::Append)) {
        delete m_currentFile;
        m_currentFile = nullptr;
        m_busy = false;
        emit failed(tr("Не удалось открыть файл для загрузки: %1").arg(m_currentInfo.path));
        return;
    }

    const qint64 offset = m_currentFile->size();
    if (offset >= m_currentInfo.size) {
        finishFile();
        return;
    }

    m_connection->request(
        QStringLiteral("update.download"),
        QJsonObject{{QStringLiteral("path"), m_currentInfo.path},
                    {QStringLiteral("offset"), static_cast<double>(offset)}},
        [this](const QJsonObject &reply) {
            if (!reply.value(QStringLiteral("ok")).toBool(false)) {
                m_busy = false;
                emit failed(core::proto::errorMessageOf(reply));
                return;
            }

            m_transferId = static_cast<quint32>(
                reply.value(QStringLiteral("data")).toObject()
                    .value(QStringLiteral("transfer_id")).toDouble());
        },
        15000);
}

void UpdateService::onChunk(quint32 transferId, quint32 sequence, const QByteArray &data)
{
    Q_UNUSED(sequence)

    if (transferId != m_transferId || !m_currentFile)
        return;

    if (m_currentFile->write(data) != data.size()) {
        emit failed(tr("Ошибка записи обновления."));
        return;
    }

    m_doneBytes += data.size();
    const int percent = m_totalBytes > 0
                            ? static_cast<int>(qMin<qint64>(100, m_doneBytes * 100 / m_totalBytes))
                            : 0;
    emit progress(percent, m_currentInfo.path);
}

void UpdateService::onTransferFinished(quint32 transferId)
{
    if (transferId != m_transferId || !m_currentFile)
        return;

    finishFile();
}

void UpdateService::finishFile()
{
    if (!m_currentFile)
        return;

    m_currentFile->close();
    const QString part = m_currentFile->fileName();
    delete m_currentFile;
    m_currentFile = nullptr;
    m_transferId = 0;

    const qint64 actualSize = QFileInfo(part).size();
    const QString actualHash = actualSize == m_currentInfo.size ? sha256OfFile(part) : QString();

    if (actualHash.compare(m_currentInfo.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(part);
        if (!m_retried) {
            m_retried = true;
            m_doneBytes -= actualSize;
            startNextFile();
            return;
        }

        m_busy = false;
        emit failed(tr("Файл повреждён при загрузке: %1").arg(m_currentInfo.path));
        return;
    }

    const QString finalPath = m_tempRoot + QLatin1Char('/') + m_currentInfo.path;
    QFile::remove(finalPath);
    if (!QFile::rename(part, finalPath)) {
        m_busy = false;
        emit failed(tr("Не удалось сохранить файл: %1").arg(m_currentInfo.path));
        return;
    }

    ++m_index;
    startNextFile();
}

bool UpdateService::launchUpdater(QString *error)
{
    const QString updater = appDirectory() + QStringLiteral("/MessengerUpdater.exe");
    if (!QFileInfo::exists(updater)) {
        if (error)
            *error = tr("Не найден MessengerUpdater.exe — обновление невозможно.");
        return false;
    }

    QJsonObject apply;
    apply.insert(QStringLiteral("version"), m_version);
    QJsonArray files;
    for (const UpdateFileInfo &info : m_files)
        files.append(info.path);
    apply.insert(QStringLiteral("files"), files);
    apply.insert(QStringLiteral("source"), m_tempRoot);

    QFile manifest(m_tempRoot + QStringLiteral("/apply.json"));
    if (manifest.open(QIODevice::WriteOnly)) {
        manifest.write(QJsonDocument(apply).toJson());
        manifest.close();
    }

    QStringList arguments{
        QStringLiteral("--source"), m_tempRoot,
        QStringLiteral("--target"), appDirectory(),
        QStringLiteral("--restart"), appDirectory() + QStringLiteral("/MessengerClient.exe"),
        QStringLiteral("--pid"), QString::number(QCoreApplication::applicationPid()),
    };

    if (!QProcess::startDetached(updater, arguments)) {
        if (error)
            *error = tr("Не удалось запустить программу обновления.");
        return false;
    }

    emit downloaded();
    QTimer::singleShot(200, QCoreApplication::instance(), &QCoreApplication::quit);
    return true;
}

} // namespace app
