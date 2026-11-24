#pragma once

#include <QHash>
#include <QHttpHeaders>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace cirrus::test
{

// A synthetic CDN for range downloads: serves `body` in blocks over time. No socket.
struct RangeServer {
    QByteArray body;
    QHash<QString, QByteArray> bodies; // by URL path, served instead of `body`
    bool honorRange = true;
    qsizetype blockBytes = 64 * 1024;
    int intervalMs = 0;
    qint64 slowAfter = -1; // from this byte of the body on, a block every slowIntervalMs
    int slowIntervalMs = 0;
    QList<int> statuses;         // forced status for the next requests (consumed in order)
    QList<QByteArray> locations; // Location of the next forced 3xx statuses (in order)
    QByteArray contentRange;     // non-empty: sent instead of the right one
    QList<QByteArray> ranges;    // the Range header of every request
    QList<QUrl> urls;
    QList<QNetworkRequest> requests;
};

class RangeReply : public QNetworkReply
{
  public:
    RangeReply(const QNetworkRequest& request, RangeServer& server, QObject* parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        const auto range = request.rawHeader("Range");
        server.ranges.append(range);
        server.urls.append(request.url());
        server.requests.append(request);
        int status = server.statuses.isEmpty() ? 0 : server.statuses.takeFirst();
        const QByteArray body = server.bodies.value(request.url().path(), server.body);
        qint64 start = 0, end = body.size() - 1;
        if (range.startsWith("bytes=")) {
            const auto dash = range.indexOf('-');
            start = range.mid(6, dash - 6).toLongLong();
            const auto last = range.mid(dash + 1);
            if (!last.isEmpty())
                end = std::min<qint64>(end, last.toLongLong());
        }
        QHttpHeaders headers;
        if (status == 0 && server.honorRange && !range.isEmpty()) {
            status = 206;
            m_data = body.mid(start, end - start + 1);
            headers.append("Content-Range", server.contentRange.isEmpty()
                                                ? "bytes " + QByteArray::number(start) + '-' +
                                                      QByteArray::number(end) + '/' +
                                                      QByteArray::number(body.size())
                                                : server.contentRange);
        } else if (status == 0) {
            status = 200;
            m_data = body;
        }
        if (status == 200 || status == 206)
            headers.append("Content-Length", QByteArray::number(m_data.size()));
        if (status >= 300 && status < 400 && !server.locations.isEmpty())
            headers.append("Location", server.locations.takeFirst());
        m_status = status;
        m_headers = headers;
        m_block = server.blockBytes;
        m_interval = server.intervalMs;
        m_start = start;
        m_slowAfter = server.slowAfter;
        m_slowInterval = server.slowIntervalMs;
        QTimer::singleShot(0, this, [this] { begin(); });
    }
    void abort() override
    {
        if (isFinished())
            return;
        m_data.clear();
        m_offset = 0;
        m_sent = 0;
        setError(OperationCanceledError, QStringLiteral("canceled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_sent - m_offset + QIODevice::bytesAvailable();
    }

  protected:
    qint64 readData(char* buffer, qint64 maximum) override
    {
        const qint64 count = std::min(maximum, m_sent - m_offset);
        if (count <= 0)
            return 0;
        std::memcpy(buffer, m_data.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

  private:
    void begin()
    {
        if (isFinished())
            return;
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_status);
        setHeaders(m_headers);
        if (m_status >= 500)
            setError(InternalServerError, QStringLiteral("synthetic"));
        else if (m_status >= 400)
            setError(ContentAccessDenied, QStringLiteral("synthetic"));
        emit metaDataChanged();
        step();
    }
    void step()
    {
        if (isFinished())
            return;
        if (m_sent < m_data.size()) {
            m_sent = std::min<qint64>(m_data.size(), m_sent + m_block);
            emit readyRead();
            if (isFinished())
                return;
            const bool slow = m_slowAfter >= 0 && m_start + m_sent >= m_slowAfter;
            QTimer::singleShot(slow ? m_slowInterval : m_interval, this, [this] { step(); });
            return;
        }
        setFinished(true);
        emit finished();
    }

    QByteArray m_data;
    QHttpHeaders m_headers;
    int m_status = 200;
    qint64 m_offset = 0, m_sent = 0;
    qsizetype m_block = 0;
    int m_interval = 0;
    qint64 m_start = 0, m_slowAfter = -1;
    int m_slowInterval = 0;
};

class RangeManager : public QNetworkAccessManager
{
  public:
    explicit RangeManager(RangeServer& server) : m_server(server) {}

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice*) override
    {
        Q_ASSERT(operation == GetOperation);
        return new RangeReply(request, m_server, this);
    }

  private:
    RangeServer& m_server;
};

} // namespace cirrus::test
