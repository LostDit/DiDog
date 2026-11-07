#pragma once

#include "core/Error.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QTimer;

namespace net {

// Учёт запросов «id → callback» с таймаутами. Ответ приходит один раз:
// либо reply, либо ошибка таймаута/обрыва соединения.
class RequestTracker : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QJsonObject &reply)>;

    explicit RequestTracker(QObject *parent = nullptr);
    ~RequestTracker() override;

    void track(const QString &requestId, Callback callback, int timeoutMs);

    // Возвращает true, если запрос с таким reply_to был найден и завершён.
    bool complete(const QString &replyTo, const QJsonObject &reply);

    void failAll(core::ErrorCode code, const QString &message = QString());
    void clear();

    int pendingCount() const { return m_pending.size(); }

private:
    struct Pending {
        Callback callback;
        QTimer *timer = nullptr;
    };

    void finish(const QString &requestId, const QJsonObject &reply);

    QHash<QString, Pending> m_pending;
};

} // namespace net
