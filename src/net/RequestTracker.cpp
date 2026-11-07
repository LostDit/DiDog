#include "net/RequestTracker.h"

#include "core/Protocol.h"

#include <QTimer>

namespace net {

RequestTracker::RequestTracker(QObject *parent)
    : QObject(parent)
{
}

RequestTracker::~RequestTracker()
{
    clear();
}

void RequestTracker::track(const QString &requestId, Callback callback, int timeoutMs)
{
    if (requestId.isEmpty())
        return;

    if (m_pending.contains(requestId))
        complete(requestId, core::proto::errorReply(requestId, core::ErrorCode::Conflict));

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);

    connect(timer, &QTimer::timeout, this, [this, requestId]() {
        if (!m_pending.contains(requestId))
            return;

        const QJsonObject reply = core::proto::errorReply(requestId, core::ErrorCode::Timeout);
        finish(requestId, reply);
    });

    m_pending.insert(requestId, Pending{std::move(callback), timer});
    timer->start(qMax(1, timeoutMs));
}

bool RequestTracker::complete(const QString &replyTo, const QJsonObject &reply)
{
    if (!m_pending.contains(replyTo))
        return false;

    finish(replyTo, reply);
    return true;
}

void RequestTracker::failAll(core::ErrorCode code, const QString &message)
{
    const QStringList ids = m_pending.keys();
    for (const QString &id : ids) {
        if (!m_pending.contains(id))
            continue;
        finish(id, core::proto::errorReply(id, code, message));
    }
}

void RequestTracker::clear()
{
    const QStringList ids = m_pending.keys();
    for (const QString &id : ids) {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            continue;

        if (it->timer) {
            it->timer->stop();
            it->timer->deleteLater();
        }
        m_pending.erase(it);
    }
}

void RequestTracker::finish(const QString &requestId, const QJsonObject &reply)
{
    auto it = m_pending.find(requestId);
    if (it == m_pending.end())
        return;

    Pending pending = *it;
    m_pending.erase(it);

    if (pending.timer) {
        pending.timer->stop();
        pending.timer->deleteLater();
    }

    if (pending.callback)
        pending.callback(reply);
}

} // namespace net
