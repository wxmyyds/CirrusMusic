#include "cover_provider.h"

#include "sources.h"

#include <QBuffer>
#include <QImageReader>
#include <QQuickWindow>
#include <QSGTexture>
#include <QThreadPool>
#include <QUrl>

namespace cirrus::ui
{

namespace
{
class CoverResponse;

// A cover's texture, never in the scene graph's atlas: CoverArt samples it in a shader of
// its own, which would take an atlas texture out into a copy (twice the memory).
class CoverTexture : public QQuickTextureFactory
{
  public:
    explicit CoverTexture(QImage image) : m_image(std::move(image)) {}
    QSGTexture* createTexture(QQuickWindow* window) const override
    {
        return window->createTextureFromImage(m_image);
    }
    [[nodiscard]] QSize textureSize() const override { return m_image.size(); }
    [[nodiscard]] int textureByteCount() const override { return int(m_image.sizeInBytes()); }
    [[nodiscard]] QImage image() const override { return m_image; }

  private:
    QImage m_image;
};

// One cover request, shared by the response (image loader thread), the source request
// (GUI thread) and the decode job (thread pool).
struct Job {
    QMutex mutex;
    CoverResponse* response = nullptr; // null once canceled or destroyed
    Request request;                   // GUI thread only
    std::weak_ptr<QObject> gui;        // the provider's GUI thread context
};

class CoverResponse : public QQuickImageResponse
{
  public:
    explicit CoverResponse(std::shared_ptr<Job> job) : m_job(std::move(job))
    {
        const QMutexLocker lock(&m_job->mutex);
        m_job->response = this;
    }
    ~CoverResponse() override { detach(); }

    QQuickTextureFactory* textureFactory() const override
    {
        return m_image.isNull() ? nullptr : new CoverTexture(m_image);
    }
    QString errorString() const override { return m_error; }

    // No longer shown (a page scrolled it away): the source request goes too, so what
    // is on screen loads first. Called on the image loader's thread.
    void cancel() override
    {
        detach();
        if (const auto gui = m_job->gui.lock())
            QMetaObject::invokeMethod(
                gui.get(), [job = m_job]() { job->request.cancel(); }, Qt::QueuedConnection);
        emit finished();
    }

    // Any thread, with the job mutex held.
    void finish(const QImage& image, const QString& error)
    {
        m_image = image;
        m_error = error;
        emit finished();
    }

  private:
    void detach()
    {
        const QMutexLocker lock(&m_job->mutex);
        m_job->response = nullptr;
    }

    std::shared_ptr<Job> m_job;
    QImage m_image;
    QString m_error;
};

void deliver(const std::shared_ptr<Job>& job, const QImage& image, const QString& error)
{
    const QMutexLocker lock(&job->mutex);
    if (job->response)
        job->response->finish(image, error);
}

// Decodes at the requested size: covering it (the Image crops or fits), keeping the
// aspect ratio, never upscaling. A 0 dimension follows the other one, as with
// Image.sourceSize.
QImage decode(const QByteArray& bytes, const QSize& requested)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (original.isValid() && (requested.width() > 0 || requested.height() > 0)) {
        const QSize bound(requested.width() > 0 ? requested.width() : original.width() * 1000,
                          requested.height() > 0 ? requested.height() : original.height() * 1000);
        const QSize target = original.scaled(bound, requested.width() > 0 && requested.height() > 0
                                                        ? Qt::KeepAspectRatioByExpanding
                                                        : Qt::KeepAspectRatio);
        if (target.width() < original.width())
            reader.setScaledSize(target);
    }
    return reader.read();
}
} // namespace

// Deleted on its own thread, whichever thread lets go of it last.
CoverProvider::CoverProvider(ImageSource* source)
    : m_source(source), m_guiContext(new QObject, [](QObject* o) { o->deleteLater(); })
{
}

CoverProvider::~CoverProvider() = default;

QQuickImageResponse* CoverProvider::requestImageResponse(const QString& id,
                                                         const QSize& requestedSize)
{
    auto job = std::make_shared<Job>();
    job->gui = m_guiContext;
    auto* response = new CoverResponse(job);
    const QString key = QUrl::fromPercentEncoding(id.toUtf8());
    const QString cacheKey =
        QStringLiteral("%1@%2x%3").arg(key).arg(requestedSize.width()).arg(requestedSize.height());

    // Replies always come after this function returned (event loop or another thread).
    QImage cached;
    {
        const QMutexLocker lock(&m_cache->mutex);
        if (const QImage* hit = m_cache->images.object(cacheKey))
            cached = *hit;
    }
    // A hit is delivered from the GUI thread's event loop, never right away from
    // another thread: a caller on the GUI thread (Theme) connects to `finished` after
    // this returns.
    if (!cached.isNull()) {
        QMetaObject::invokeMethod(
            m_guiContext.get(), [job, cached]() { deliver(job, cached, {}); },
            Qt::QueuedConnection);
        return response;
    }

    QMetaObject::invokeMethod(
        m_guiContext.get(),
        [job, key, cacheKey, requestedSize, cache = m_cache, source = m_source]() {
            {
                const QMutexLocker lock(&job->mutex);
                if (!job->response)
                    return;
            }
            job->request = source->image(
                key, qMax(requestedSize.width(), requestedSize.height()),
                [job, cacheKey, requestedSize, cache](Outcome<QByteArray> outcome) {
                    job->request.release();
                    if (!outcome.value) {
                        // No key in the text: QML logs it, and a key may be an address.
                        deliver(job, {}, QStringLiteral("cover is unavailable"));
                        return;
                    }
                    QThreadPool::globalInstance()->start(
                        [job, cacheKey, requestedSize, cache, bytes = *outcome.value]() {
                            const QImage image = decode(bytes, requestedSize);
                            if (image.isNull()) {
                                deliver(job, {}, QStringLiteral("cannot decode cover"));
                                return;
                            }
                            {
                                const QMutexLocker lock(&cache->mutex);
                                cache->images.insert(
                                    cacheKey, new QImage(image),
                                    int(qMax<qsizetype>(1, image.sizeInBytes() / 1024)));
                            }
                            deliver(job, image, {});
                        });
                });
        },
        Qt::QueuedConnection);
    return response;
}

} // namespace cirrus::ui
