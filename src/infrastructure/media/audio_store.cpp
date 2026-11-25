#include "audio_store.h"
#include "infrastructure/storage/private_files.h"
#include "logging/log.h"
#include "media_source_policy.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <chrono>

namespace cirrus::media
{
using namespace domain;
namespace
{
constexpr int kMaximumRetries = 3;
constexpr int kMaximumRedirects = 3;
constexpr qint64 kReadBlock = 64 * 1024;
constexpr qint64 kMetaEvery = 4 * 1024 * 1024;
constexpr int kMetaVersion = 1;
constexpr qint64 kAnalysisMaximumBytes = 1024 * 1024; // a few KB each: larger is damaged

bool validMd5(const QByteArray& md5)
{
    return md5.size() == 32 && std::all_of(md5.begin(), md5.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

// The cache identity as one file-name token: the provider's md5 (content-addressed) or its
// stream name, sanitized (stream-addressed). Empty when the source can't be kept.
QString identityToken(const MediaSource& source)
{
    if (const auto* hash = std::get_if<ContentHash>(&source.identity))
        return validMd5(hash->md5) ? QString::fromLatin1(hash->md5) : QString{};
    const auto* key = std::get_if<StreamKey>(&source.identity);
    if (!key || key->key.isEmpty())
        return {};
    QString safe;
    safe.reserve(key->key.size());
    for (const auto c : key->key)
        safe.append(c.isLetterOrNumber() || c == u'_' || c == u'-' || c == u'.' ? c : u'_');
    if (safe.isEmpty() || safe.size() > 160)
        return {};
    return QStringLiteral("s-") + safe;
}

bool contentAddressed(const MediaSource& source)
{
    return std::holds_alternative<ContentHash>(source.identity);
}

// The same rules as the resource cache (storage/private_files.h).
using storage::makePrivateDirectory;
using storage::openPrivateFile;

QByteArray packBits(const std::vector<bool>& bits)
{
    QByteArray out((qsizetype(bits.size()) + 7) / 8, '\0');
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i])
            out[qsizetype(i / 8)] = char(out[qsizetype(i / 8)] | (1 << (i % 8)));
    return out;
}
std::optional<std::vector<bool>> unpackBits(const QByteArray& packed, size_t count)
{
    if (packed.size() != qsizetype((count + 7) / 8))
        return std::nullopt;
    std::vector<bool> bits(count);
    for (size_t i = 0; i < count; ++i)
        bits[i] = (packed[qsizetype(i / 8)] >> (i % 8)) & 1;
    return bits;
}
qint64 chunkEnd(const AudioStreamState& state, size_t chunk)
{
    return std::min(state.size, qint64(chunk + 1) * state.chunkBytes);
}
} // namespace

// ---- shared state and reader --------------------------------------------------------

bool AudioStreamState::available(qint64 position) const
{
    if (position >= activeStart && position < activeEnd)
        return true;
    const auto chunk = size_t(position / chunkBytes);
    return chunk < have.size() && have[chunk];
}
bool AudioStreamState::complete() const
{
    return std::all_of(have.begin(), have.end(), [](bool b) { return b; });
}

struct AudioStore::Download {
    quint64 id = 0;
    QString base; // partition dir + md5-size; empty: a temporary file
    MediaSource source;
    QUrl url;
    std::shared_ptr<AudioStreamState> state;
    std::unique_ptr<QFile> writer;
    QPointer<QNetworkReply> reply;
    qint64 requestStart = 0, requestEnd = 0, position = 0;
    qint64 discard = 0; // the server ignored Range: bytes to skip
    bool headersChecked = false;
    int redirects = 0, retries = 0, handles = 0;
    qint64 prefetchLimit = -1; // >= 0: download only this much, nobody reads yet
    bool waitingUrl = false, verifying = false, discardOnClose = false;
    bool verifiedComplete = false; // the whole file passed its md5 check
    qint64 sinceMeta = 0;
    QTimer retryTimer, refreshTimer;
};

AudioStream::AudioStream(std::shared_ptr<AudioStreamState> state, QPointer<AudioStore> store,
                         quint64 id)
    : m_state(std::move(state)), m_store(std::move(store)), m_id(id)
{
}
AudioStream::~AudioStream()
{
    m_file.reset();
    if (auto* store = m_store.data())
        QMetaObject::invokeMethod(
            store,
            [store = m_store, id = m_id] {
                if (store)
                    store->release(id);
            },
            Qt::QueuedConnection);
}
qint64 AudioStream::size() const { return m_state->size; }
bool AudioStream::complete() const
{
    std::lock_guard lock(m_state->lock);
    return m_state->complete();
}
std::optional<PlaybackError> AudioStream::failure() const
{
    std::lock_guard lock(m_state->lock);
    return m_state->failure;
}
void AudioStream::cancel()
{
    {
        std::lock_guard lock(m_state->lock);
        m_canceled = true;
    }
    m_state->changed.notify_all();
}
void AudioStream::interrupt()
{
    {
        std::lock_guard lock(m_state->lock);
        m_interruptPending = true;
    }
    m_state->changed.notify_all();
}
void AudioStream::clearInterrupt()
{
    std::lock_guard lock(m_state->lock);
    m_interruptPending = false;
}
qint64 AudioStream::read(qint64 position, char* out, qint64 maximum)
{
    auto& s = *m_state;
    if (position < 0 || maximum <= 0)
        return -1;
    if (position >= s.size)
        return 0;
    qint64 end = 0;
    {
        std::unique_lock lock(s.lock);
        const bool moved = s.wanted != position;
        s.wanted = position;
        bool asked = false;
        while (!s.available(position)) {
            if (m_canceled || s.failure)
                return -1;
            // Where we are goes to the downloader first, also when this read ends
            // interrupted: a seek's read past the download must move it (2026-11-22).
            if (moved && !asked) {
                asked = true; // tell the downloader once where we are
                lock.unlock();
                if (auto* store = m_store.data())
                    QMetaObject::invokeMethod(
                        store,
                        [store = m_store, id = m_id] {
                            if (store)
                                store->want(id);
                        },
                        Qt::QueuedConnection);
                lock.lock();
                continue;
            }
            if (m_interruptPending) {
                m_interruptPending = false;
                return -1;
            }
            s.changed.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (m_canceled)
            return -1;
        // The contiguous bytes from here: whole chunks, then the running request.
        auto chunk = size_t(position / s.chunkBytes);
        end = position;
        while (chunk < s.have.size() && s.have[chunk] && chunkEnd(s, chunk) > end)
            end = chunkEnd(s, chunk++);
        if (position >= s.activeStart && position < s.activeEnd)
            end = std::max(end, s.activeEnd);
    }
    if (!m_file) {
        // Unbuffered: a buffered handle reads ahead past the download head into the
        // file's unwritten (zero) bytes and later serves them from its buffer.
        m_file = openPrivateFile(s.path, QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (!m_file)
            return -1;
    }
    if (!m_file->seek(position))
        return -1;
    const qint64 got = m_file->read(out, std::min(maximum, end - position));
    return got > 0 ? got : -1;
}

// ---- store ------------------------------------------------------------------------

AudioStore::AudioStore(Options options, std::unique_ptr<QNetworkAccessManager> manager,
                       QObject* parent)
    : QObject(parent), m_options(std::move(options)),
      m_manager(manager ? std::move(manager) : std::make_unique<QNetworkAccessManager>())
{
    Q_ASSERT(m_manager->thread() == thread() && !m_manager->parent());
    m_rootUsable = !m_options.root.isEmpty() && makePrivateDirectory(m_options.root);
    if (!m_options.root.isEmpty() && !m_rootUsable) {
        const storage::Refusal why = storage::lastRefusal();
        log::warn(lcMedia, "audio_cache.root_unusable",
                  {{"dir", log::path(m_options.root)},
                   {"reason", QString::fromLatin1(why.reason ? why.reason : "-")},
                   log::fieldIf(why.error != 0, "os_error", qint64(why.error))});
    }
}
AudioStore::~AudioStore()
{
    const auto ids = m_downloads.keys();
    for (const auto id : ids)
        retire(id);
}

void AudioStore::setPartition(const QString& partition)
{
    const auto ids = m_downloads.keys();
    for (const auto id : ids) {
        if (auto d = m_downloads.value(id); d && d->handles > 0)
            fail(*d, PlaybackError::StaleContext);
        else
            retire(id);
    }
    m_partitionDir.clear();
    if (!m_rootUsable || partition.isEmpty())
        return;
    const auto name = QString::fromLatin1(
        QCryptographicHash::hash(partition.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
    const auto dir = m_options.root + u'/' + name;
    if (makePrivateDirectory(dir))
        m_partitionDir = dir;
    enforceCapacity();
}

QString AudioStore::baseFor(const MediaSource& source) const
{
    const auto token = identityToken(source);
    if (m_partitionDir.isEmpty() || token.isEmpty())
        return {};
    return m_partitionDir + u'/' + token + u'-' + QString::number(source.size);
}

AudioStore::Download* AudioStore::findByBase(const QString& base)
{
    for (auto& d : m_downloads)
        if (!base.isEmpty() && d->base == base)
            return d.get();
    return nullptr;
}

bool AudioStore::isOpenPath(const QString& path) const
{
    for (const auto& d : m_downloads)
        if (d->state->path == path)
            return true;
    return false;
}

std::shared_ptr<AudioStream> AudioStore::open(const MediaSource& source, PlaybackError* failure)
{
    Q_ASSERT(QThread::currentThread() == thread());
    Download* d = findByBase(baseFor(source));
    if (d) {
        log::debug(
            lcMedia, "audio.open_shared",
            {{"dl", d->id}, {"prefetched", d->prefetchLimit >= 0}, {"handles", d->handles + 1}});
        d->prefetchLimit = -1; // a prefetch becomes the real thing
        if (!d->waitingUrl) {
            d->url = source.url; // the fresher address
            d->source.validUntil = source.validUntil;
        }
    } else if (!(d = create(source, failure))) {
        return {};
    }
    ++d->handles;
    QTimer::singleShot(0, this, [this, id = d->id] {
        if (auto d = m_downloads.value(id))
            schedule(*d);
    });
    return std::shared_ptr<AudioStream>(new AudioStream(d->state, this, d->id));
}

AudioStore::Download* AudioStore::create(const MediaSource& source, PlaybackError* failure)
{
    const auto refuse = [failure, &source](PlaybackError error) -> Download* {
        log::warn(lcMedia, "audio.open_refused",
                  {{"err", error}, {"host", log::host(source.url)}, {"bytes", source.size}});
        if (failure)
            *failure = error;
        return nullptr;
    };
    if (!approvedMediaUrl(source.provider, source.url))
        return refuse(PlaybackError::UnsafeSource);
    if (source.size <= 0 || source.size > m_options.maximumFileBytes)
        return refuse(PlaybackError::TooLarge);
    auto d = std::make_shared<Download>();
    d->id = ++m_nextId;
    d->base = baseFor(source);
    d->source = source;
    d->url = source.url;
    auto state = std::make_shared<AudioStreamState>();
    state->size = source.size;
    state->chunkBytes = m_options.chunkBytes;
    state->have.assign(size_t((source.size + m_options.chunkBytes - 1) / m_options.chunkBytes),
                       false);
    if (!d->base.isEmpty()) {
        state->path = d->base + QStringLiteral(".audio");
        d->writer = openPrivateFile(state->path);
        if (d->writer) {
            QFile meta(d->base + QStringLiteral(".meta"));
            const auto json = meta.open(QIODevice::ReadOnly)
                                  ? QJsonDocument::fromJson(meta.read(1024 * 1024)).object()
                                  : QJsonObject();
            const bool matches =
                json.value(QStringLiteral("v")).toInt() == kMetaVersion &&
                json.value(QStringLiteral("size")).toInteger() == source.size &&
                json.value(QStringLiteral("identity")).toString() == identityToken(source) &&
                json.value(QStringLiteral("chunk")).toInteger() == m_options.chunkBytes &&
                d->writer->size() == source.size;
            if (matches) {
                if (auto bits =
                        unpackBits(QByteArray::fromBase64(
                                       json.value(QStringLiteral("have")).toString().toLatin1()),
                                   state->have.size())) {
                    state->have = std::move(*bits);
                    d->verifiedComplete =
                        json.value(QStringLiteral("complete")).toBool() && state->complete();
                }
            }
            if (d->writer->size() != source.size && !d->writer->resize(source.size))
                d->writer.reset();
        }
        if (!d->writer)
            d->base.clear(); // an unusable cache file: stream through a temporary one
    }
    if (d->base.isEmpty()) {
        auto temporary = std::make_unique<QTemporaryFile>(QDir::tempPath() +
                                                          QStringLiteral("/cirrus-audio-XXXXXX"));
        if (!temporary->open() || !temporary->resize(source.size))
            return refuse(PlaybackError::Storage);
        state->path = temporary->fileName();
        state->have.assign(state->have.size(), false);
        d->verifiedComplete = false;
        d->writer = std::move(temporary);
    }
    // What playback starts from (docs/tasks/logging.md §8): how much is on disk already.
    const auto have = std::count(state->have.cbegin(), state->have.cend(), true);
    log::debug(
        lcMedia, "audio.open",
        {{"dl", d->id},
         {"bytes", source.size},
         {"format", source.format},
         {"cached_pct", state->have.empty() ? 0 : int(have * 100 / qsizetype(state->have.size()))},
         {"verified", d->verifiedComplete},
         {"temporary", d->base.isEmpty()},
         {"host", log::host(source.url)}});
    d->state = std::move(state);
    m_downloads.insert(d->id, d);
    saveMeta(*d); // records the use (eviction order)
    return d.get();
}

void AudioStore::prefetch(const MediaSource& source, qint64 bytes)
{
    const auto base = baseFor(source);
    if (base.isEmpty() || bytes <= 0 || findByBase(base))
        return; // nothing to keep it under, or already there
    auto* d = create(source, nullptr);
    if (!d)
        return;
    d->prefetchLimit = std::min(source.size, bytes);
    log::debug(lcMedia, "audio.prefetch", {{"dl", d->id}, {"bytes", d->prefetchLimit}});
    QTimer::singleShot(0, this, [this, id = d->id] {
        if (auto d = m_downloads.value(id))
            schedule(*d);
    });
}

void AudioStore::refresh(quint64 stream, QUrl url, QDeadlineTimer validUntil)
{
    const auto d = m_downloads.value(stream);
    if (!d || !d->waitingUrl)
        return;
    if (!approvedMediaUrl(d->source.provider, url, true)) {
        fail(*d, PlaybackError::UnsafeSource);
        return;
    }
    log::info(lcMedia, "audio.url_refreshed", {"dl", d->id});
    d->url = *approvedMediaUrl(d->source.provider, url, true);
    d->source.validUntil = validUntil;
    d->waitingUrl = false;
    d->refreshTimer.stop();
    d->retries = 0;
    schedule(*d);
}

void AudioStore::release(quint64 id)
{
    const auto d = m_downloads.value(id);
    if (!d)
        return;
    if (d->handles > 0)
        --d->handles;
    if (d->handles == 0 && d->prefetchLimit < 0)
        retire(id);
}

void AudioStore::want(quint64 id)
{
    const auto d = m_downloads.value(id);
    if (!d)
        return;
    if (d->reply) {
        qint64 wanted;
        bool there;
        {
            std::lock_guard lock(d->state->lock);
            wanted = d->state->wanted;
            there = wanted < 0 || d->state->available(wanted);
        }
        const bool coming = wanted >= d->requestStart && wanted < d->requestEnd &&
                            wanted < d->position + m_options.aheadBytes;
        if (there || coming)
            return;
        abortReply(*d); // a seek elsewhere: start over there; what came stays
    }
    schedule(*d);
}

void AudioStore::abortReply(Download& d)
{
    auto* reply = d.reply.data();
    d.reply = nullptr;
    {
        std::lock_guard lock(d.state->lock);
        d.state->activeStart = d.state->activeEnd = 0;
    }
    if (!reply)
        return;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void AudioStore::schedule(Download& d)
{
    if (d.reply || d.waitingUrl || d.verifying || d.retryTimer.isActive())
        return;
    if (d.handles == 0 && d.prefetchLimit < 0)
        return;
    qint64 start = -1, end = 0;
    {
        std::lock_guard lock(d.state->lock);
        auto& s = *d.state;
        if (s.failure)
            return;
        const auto count = s.have.size();
        const size_t from = s.wanted >= 0 ? size_t(s.wanted / s.chunkBytes) : 0;
        size_t target = count;
        for (size_t i = 0; i < count && target == count; ++i) {
            const size_t c = (from + i) % count; // from the reader on, then wrap
            if (!s.have[c])
                target = c;
        }
        if (target == count) {
            // Everything is there.
        } else {
            start = qint64(target) * s.chunkBytes;
            size_t stop = target;
            while (stop < count && !s.have[stop])
                ++stop;
            end = std::min(s.size, qint64(stop) * s.chunkBytes);
        }
    }
    if (start < 0) {
        completed(d);
        return;
    }
    if (d.prefetchLimit >= 0) {
        if (start >= d.prefetchLimit) {
            retire(d.id); // the head is there and stays cached; open() goes on from it
            return;
        }
        end = std::min(end, ((d.prefetchLimit + m_options.chunkBytes - 1) / m_options.chunkBytes) *
                                m_options.chunkBytes);
        end = std::min(end, d.state->size);
    }
    request(d, start, end);
}

void AudioStore::request(Download& d, qint64 start, qint64 end)
{
    if (d.source.validUntil.isForever() || d.source.validUntil.hasExpired()) {
        needUrl(d);
        return;
    }
    QNetworkRequest request(d.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setRawHeader("Accept-Encoding", "identity");
    // Provider headers: Bilibili's CDN needs its Referer and User-Agent; Netease needs none.
    // The store never sends a Cookie (none is in MediaSource).
    if (d.source.provider == domain::SourceProvider::Bilibili) {
        if (!d.source.referer.isEmpty())
            request.setRawHeader("Referer", d.source.referer.toUtf8());
        if (!d.source.userAgent.isEmpty())
            request.setRawHeader("User-Agent", d.source.userAgent.toUtf8());
    }
    request.setRawHeader("Range",
                         "bytes=" + QByteArray::number(start) + '-' + QByteArray::number(end - 1));
    request.setTransferTimeout(15000);
    d.requestStart = start;
    d.requestEnd = end;
    d.position = start;
    d.discard = 0;
    d.headersChecked = false;
    {
        std::lock_guard lock(d.state->lock);
        d.state->activeStart = d.state->activeEnd = start;
    }
    log::debug(lcMedia, "audio.range",
               {{"dl", d.id}, {"from", start}, {"to", end}, {"host", log::host(d.url)}});
    d.reply = m_manager->get(request);
    d.reply->setReadBufferSize(256 * 1024);
    const auto id = d.id;
    connect(d.reply, &QIODevice::readyRead, this, [this, id] { consume(id); });
    connect(d.reply, &QNetworkReply::finished, this, [this, id] { finished(id); });
}

void AudioStore::consume(quint64 id)
{
    const auto d = m_downloads.value(id);
    if (!d || !d->reply)
        return;
    auto* reply = d->reply.data();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 && status != 206)
        return; // redirects and refusals are handled when finished
    if (!d->headersChecked) {
        const auto encoding = reply->rawHeader("Content-Encoding").toLower();
        if (!encoding.isEmpty() && encoding != "identity") {
            fail(*d, PlaybackError::InvalidResponse);
            return;
        }
        if (status == 206) {
            // "bytes a-b/total" for exactly what was asked, of the file we expect.
            const auto range = reply->rawHeader("Content-Range");
            const auto slash = range.indexOf('/');
            const auto dash = range.indexOf('-');
            bool okA = false, okB = false, okT = false;
            const qint64 a = range.startsWith("bytes ") && dash > 6
                                 ? range.mid(6, dash - 6).toLongLong(&okA)
                                 : -1;
            const qint64 b =
                slash > dash ? range.mid(dash + 1, slash - dash - 1).toLongLong(&okB) : -1;
            const qint64 total = slash > 0 ? range.mid(slash + 1).toLongLong(&okT) : -1;
            if (!okA || !okB || !okT || a != d->requestStart || total != d->state->size || b < a ||
                b >= total) {
                fail(*d, PlaybackError::InvalidResponse);
                return;
            }
            d->requestEnd = std::min(d->requestEnd, b + 1);
        } else {
            // The whole file despite Range: skip to where we asked.
            const auto length = reply->rawHeader("Content-Length");
            bool ok = false;
            if (!length.isEmpty() && (length.toLongLong(&ok) != d->state->size || !ok)) {
                fail(*d, PlaybackError::TooLarge);
                return;
            }
            d->discard = d->requestStart;
        }
        d->headersChecked = true;
    }
    qint64 wrote = 0;
    while (d->reply && reply->bytesAvailable() > 0 && d->position < d->requestEnd) {
        auto block = reply->read(kReadBlock);
        if (block.isEmpty())
            break;
        if (d->discard > 0) {
            const auto skip = std::min<qint64>(d->discard, block.size());
            d->discard -= skip;
            block.remove(0, skip);
            if (block.isEmpty())
                continue;
        }
        const auto take = std::min<qint64>(block.size(), d->requestEnd - d->position);
        if (!d->writer->seek(d->position) || d->writer->write(block.constData(), take) != take) {
            fail(*d, PlaybackError::Storage);
            return;
        }
        d->position += take;
        wrote += take;
    }
    if (wrote > 0) {
        d->writer->flush(); // readers use their own handle
        {
            std::lock_guard lock(d->state->lock);
            auto& s = *d->state;
            s.activeEnd = d->position;
            // Requests start on a chunk; every chunk up to the write head is whole.
            for (auto c = size_t(d->requestStart / s.chunkBytes);
                 c < s.have.size() && chunkEnd(s, c) <= d->position; ++c)
                s.have[c] = true;
        }
        d->state->changed.notify_all();
        d->sinceMeta += wrote;
        if (!d->base.isEmpty() && d->sinceMeta >= kMetaEvery) {
            d->sinceMeta = 0;
            saveMeta(*d);
        }
    }
    if (d->reply && d->position >= d->requestEnd) {
        abortReply(*d); // got what was asked; the server may keep sending (200)
        d->retries = 0;
        d->redirects = 0;
        schedule(*d);
    }
}

void AudioStore::finished(quint64 id)
{
    const auto d = m_downloads.value(id);
    if (!d || !d->reply)
        return;
    consume(id);
    if (!d->reply)
        return; // consume() finished or failed it
    auto* reply = d->reply.data();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    const auto location = reply->rawHeader("Location");
    const auto base = reply->url();
    abortReply(*d);
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const auto url = base.resolved(QUrl::fromEncoded(location, QUrl::StrictMode));
        if (++d->redirects > kMaximumRedirects || location.isEmpty() || location.size() > 8192 ||
            !approvedMediaUrl(d->source.provider, url)) {
            fail(*d, PlaybackError::UnsafeSource);
            return;
        }
        log::debug(lcMedia, "audio.redirect",
                   {{"dl", d->id}, {"status", status}, {"host", log::host(url)}});
        d->url = url;
        schedule(*d);
        return;
    }
    if (status == 403 || status == 404 || status == 410) {
        log::info(lcMedia, "audio.url_refused", {{"dl", d->id}, {"status", status}});
        needUrl(*d);
        return;
    }
    if (error != QNetworkReply::NoError || (status != 200 && status != 206) ||
        d->position < d->requestEnd) {
        log::warn(lcMedia, "audio.range_failed",
                  {{"dl", d->id},
                   {"status", status},
                   {"qt", error},
                   {"at", d->position},
                   {"to", d->requestEnd},
                   {"retry", d->retries + 1},
                   {"h2", reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()}});
        retry(*d);
        return;
    }
    d->retries = 0;
    d->redirects = 0;
    schedule(*d);
}

void AudioStore::retry(Download& d)
{
    if (++d.retries > kMaximumRetries) {
        fail(d, PlaybackError::Network);
        return;
    }
    d.retryTimer.setSingleShot(true);
    d.retryTimer.setInterval(m_options.retryDelayMs << (d.retries - 1));
    disconnect(&d.retryTimer, nullptr, this, nullptr);
    connect(&d.retryTimer, &QTimer::timeout, this, [this, id = d.id] {
        if (auto d = m_downloads.value(id))
            schedule(*d);
    });
    d.retryTimer.start();
}

void AudioStore::needUrl(Download& d)
{
    if (d.handles == 0) {
        retire(d.id); // a prefetch: not worth asking
        return;
    }
    if (d.waitingUrl)
        return;
    log::debug(lcMedia, "audio.url_needed", {"dl", d.id});
    d.waitingUrl = true;
    d.refreshTimer.setSingleShot(true);
    d.refreshTimer.setInterval(m_options.refreshTimeoutMs);
    disconnect(&d.refreshTimer, nullptr, this, nullptr);
    connect(&d.refreshTimer, &QTimer::timeout, this, [this, id = d.id] {
        if (auto d = m_downloads.value(id))
            fail(*d, PlaybackError::Expired);
    });
    d.refreshTimer.start();
    emit urlNeeded(d.id);
}

void AudioStore::fail(Download& d, PlaybackError error)
{
    log::warn(lcMedia, "audio.failed", {{"dl", d.id}, {"err", error}, {"handles", d.handles}});
    abortReply(d);
    d.retryTimer.stop();
    d.refreshTimer.stop();
    d.waitingUrl = false;
    {
        std::lock_guard lock(d.state->lock);
        if (!d.state->failure)
            d.state->failure = error;
    }
    d.state->changed.notify_all();
    if (d.handles == 0)
        retire(d.id);
}

void AudioStore::completed(Download& d)
{
    if (d.base.isEmpty() || d.verifying || d.verifiedComplete)
        return; // a temporary file is complete as it is
    d.verifying = true;
    d.writer->flush();
    if (!contentAddressed(d.source)) {
        // Stream-addressed (Bilibili): no digest to check. A finished file is trusted as it is;
        // a damaged one fails to decode and asks for the address again on the next open.
        d.verifying = false;
        d.verifiedComplete = true;
        log::debug(lcMedia, "audio.cached", {{"dl", d.id}, {"bytes", d.state->size}});
        if (d.handles == 0 && d.prefetchLimit >= 0)
            retire(d.id);
        else
            saveMeta(d);
        enforceCapacity();
        return;
    }
    const auto md5 = contentMd5(d.source);
    // Hash off the owner thread; up to the file limit.
    QThreadPool::globalInstance()->start([self = QPointer<AudioStore>(this), base = d.base,
                                          path = d.state->path, size = d.state->size, md5] {
        auto file = openPrivateFile(path, QIODevice::ReadOnly);
        QCryptographicHash hash(QCryptographicHash::Md5);
        bool good = file && file->size() == size;
        while (good && !file->atEnd()) {
            const auto block = file->read(1024 * 1024);
            if (block.isEmpty())
                good = false;
            hash.addData(block);
        }
        good = good && hash.result().toHex() == md5;
        // Verification may unlink an invalid file on the owner thread immediately.
        file.reset();
        QMetaObject::invokeMethod(
            self.data(),
            [self, base, size, md5, good] {
                if (self)
                    self->verified(base, size, md5, good);
            },
            Qt::QueuedConnection);
    });
}

void AudioStore::verified(QString base, qint64 size, QByteArray md5, bool good)
{
    auto* d = findByBase(base);
    if (d)
        d->verifying = false;
    if (!good) {
        log::warn(lcMedia, "audio_cache.md5_mismatch", {"bytes", size});
        // Gone from the cache now; open handles keep reading the unlinked file, and the
        // next open() downloads afresh instead of reusing this one.
        QFile::remove(base + QStringLiteral(".meta"));
        QFile::remove(base + QStringLiteral(".audio"));
        QFile::remove(base + QStringLiteral(".analysis")); // of the wrong audio
        if (d)
            d->base.clear();
        return;
    }
    log::debug(lcMedia, "audio.cached", {{"dl", d ? d->id : 0}, {"bytes", size}});
    if (d) {
        d->verifiedComplete = true;
        saveMeta(*d);
        if (d->handles == 0 && d->prefetchLimit >= 0)
            retire(d->id);
    } else {
        Download done; // released before the check finished
        done.base = base;
        done.source.size = size;
        done.source.identity = ContentHash{md5};
        done.verifiedComplete = true;
        done.state = std::make_shared<AudioStreamState>();
        done.state->have.assign(size_t((size + m_options.chunkBytes - 1) / m_options.chunkBytes),
                                true);
        saveMeta(done);
    }
    enforceCapacity();
}

void AudioStore::retire(quint64 id)
{
    const auto d = m_downloads.take(id);
    if (!d)
        return;
    abortReply(*d);
    d->retryTimer.stop();
    d->refreshTimer.stop();
    d->writer.reset(); // a temporary file goes with it
    if (d->base.isEmpty())
        return;
    if (d->discardOnClose) {
        QFile::remove(d->base + QStringLiteral(".audio"));
        QFile::remove(d->base + QStringLiteral(".meta"));
        QFile::remove(d->base + QStringLiteral(".analysis"));
        return;
    }
    saveMeta(*d); // what was downloaded stays; an unverified whole file verifies next time
    enforceCapacity();
}

void AudioStore::saveMeta(const Download& d) const
{
    if (d.base.isEmpty())
        return;
    QByteArray bits;
    {
        std::lock_guard lock(d.state->lock);
        auto have = d.state->have;
        if (!d.verifiedComplete && !have.empty() &&
            std::all_of(have.begin(), have.end(), [](bool b) { return b; }))
            have.back() = false; // unverified: the last chunk comes again and triggers the check
        bits = packBits(have);
    }
    QJsonObject json{{QStringLiteral("v"), kMetaVersion},
                     {QStringLiteral("size"), d.source.size},
                     {QStringLiteral("identity"), identityToken(d.source)},
                     {QStringLiteral("chunk"), m_options.chunkBytes},
                     {QStringLiteral("have"), QString::fromLatin1(bits.toBase64())},
                     {QStringLiteral("complete"), d.verifiedComplete},
                     {QStringLiteral("lastUsed"), QDateTime::currentMSecsSinceEpoch()}};
    const auto path = d.base + QStringLiteral(".meta");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    if (file.commit())
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
}

QList<AudioStore::Entry> AudioStore::entries(const QString& dir) const
{
    QList<Entry> list;
    if (!m_rootUsable)
        return list;
    QDirIterator it(dir.isEmpty() ? m_options.root : dir, {QStringLiteral("*.audio")},
                    QDir::Files | QDir::NoSymLinks,
                    dir.isEmpty() ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        Entry e;
        e.data = it.next();
        e.meta = e.data.chopped(6) + QStringLiteral(".meta");
        e.analysis = e.data.chopped(6) + QStringLiteral(".analysis");
        QFile meta(e.meta);
        const auto json = meta.open(QIODevice::ReadOnly)
                              ? QJsonDocument::fromJson(meta.read(1024 * 1024)).object()
                              : QJsonObject();
        const auto size = json.value(QStringLiteral("size")).toInteger();
        const auto chunk = json.value(QStringLiteral("chunk")).toInteger();
        if (json.value(QStringLiteral("complete")).toBool()) {
            e.bytes = size;
        } else if (chunk > 0) {
            const auto bits =
                QByteArray::fromBase64(json.value(QStringLiteral("have")).toString().toLatin1());
            qint64 count = 0;
            for (const char byte : bits)
                count += qPopulationCount(quint8(byte));
            e.bytes = std::min(size, count * chunk);
        }
        e.lastUsed = json.value(QStringLiteral("lastUsed")).toInteger(); // 0: no meta, oldest
        list.append(e);
    }
    return list;
}

void AudioStore::enforceCapacity()
{
    auto list = entries({});
    qint64 total = 0;
    for (const auto& e : std::as_const(list))
        total += e.bytes;
    std::sort(list.begin(), list.end(),
              [](const Entry& a, const Entry& b) { return a.lastUsed < b.lastUsed; });
    int files = 0;
    qint64 freed = 0;
    for (const auto& e : std::as_const(list)) {
        if (total <= m_options.capacityBytes && e.lastUsed > 0)
            break;
        if (isOpenPath(e.data))
            continue;
        QFile::remove(e.data);
        QFile::remove(e.meta);
        QFile::remove(e.analysis);
        total -= e.bytes;
        freed += e.bytes;
        ++files;
    }
    if (files)
        log::info(lcMedia, "audio_cache.evicted",
                  {{"files", files},
                   {"bytes", freed},
                   {"left", total},
                   {"capacity", m_options.capacityBytes}});
}

void AudioStore::setCapacity(qint64 bytes)
{
    m_options.capacityBytes = std::max<qint64>(bytes, 0);
    if (m_rootUsable)
        enforceCapacity();
}

qint64 AudioStore::usage() const
{
    qint64 total = 0;
    for (const auto& e : entries({}))
        total += e.bytes;
    return total;
}

void AudioStore::clear(bool allPartitions)
{
    if (!allPartitions && m_partitionDir.isEmpty())
        return;
    log::info(lcMedia, "audio_cache.clear", {"all", allPartitions});
    const QString dir = allPartitions ? QString() : m_partitionDir;
    for (const auto& e : entries(dir)) {
        QFile::remove(e.meta);
        if (!isOpenPath(e.data)) {
            QFile::remove(e.data);
            continue;
        }
        for (auto& d : m_downloads)
            if (d->state->path == e.data)
                d->discardOnClose = true;
    }
    // Every analysis, also one whose file is gone already (or still open: kept in memory).
    QDirIterator it(dir.isEmpty() ? m_options.root : dir, {QStringLiteral("*.analysis")},
                    QDir::Files | QDir::NoSymLinks,
                    dir.isEmpty() ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext())
        QFile::remove(it.next());
}

QByteArray AudioStore::analysis(const MediaSource& source) const
{
    const auto base = baseFor(source);
    if (base.isEmpty())
        return {};
    QFile file(base + QStringLiteral(".analysis"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > kAnalysisMaximumBytes)
        return {};
    return file.readAll();
}

void AudioStore::keepAnalysis(const MediaSource& source, const QByteArray& bytes)
{
    const auto base = baseFor(source);
    // Only beside its file: not for one that is gone (evicted, cleared) meanwhile.
    if (base.isEmpty() || bytes.isEmpty() || bytes.size() > kAnalysisMaximumBytes ||
        !QFileInfo::exists(base + QStringLiteral(".audio")))
        return;
    const auto path = base + QStringLiteral(".analysis");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(bytes);
    if (file.commit())
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
}
} // namespace cirrus::media
