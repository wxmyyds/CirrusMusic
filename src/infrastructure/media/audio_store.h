#pragma once
#include "byte_source.h"
#include "domain/playback/playback_types.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class QFile;
class QNetworkReply;

namespace cirrus::media
{
class AudioStore;

// What a reader and the downloader share: which bytes of the file are there. Internal;
// readers use AudioStream.
struct AudioStreamState {
    mutable std::mutex lock;
    std::condition_variable changed;
    QString path;
    qint64 size = 0;
    qint64 chunkBytes = 0;
    std::vector<bool> have; // whole chunks on disk
    qint64 activeStart = 0; // the running request has written [activeStart, activeEnd)
    qint64 activeEnd = 0;
    qint64 wanted = -1; // the reader's last position, a hint for the downloader
    std::optional<domain::PlaybackError> failure;
    [[nodiscard]] bool available(qint64 position) const; // lock held
    [[nodiscard]] bool complete() const;                 // lock held
};

// One audio file being played: reads block until the bytes are downloaded (on the
// decoder's thread, never the owner's). Releasing the last handle stops its download;
// what was downloaded stays cached.
class AudioStream final : public ByteSource
{
  public:
    ~AudioStream() override;
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    [[nodiscard]] qint64 size() const override;
    // The download's id (AudioStore::urlNeeded, refresh()).
    [[nodiscard]] quint64 id() const { return m_id; }
    // Up to `maximum` bytes at `position`: > 0 read, 0 at the end, -1 failed or canceled.
    qint64 read(qint64 position, char* out, qint64 maximum) override;
    // Wakes a blocked read (it returns -1); later reads fail too.
    void cancel();
    // The read that is waiting, or the next one that has to wait, returns -1; reads after
    // that work again. Sticky so it can't slip past a read that hasn't started yet.
    void interrupt();
    void clearInterrupt();
    [[nodiscard]] bool complete() const;
    [[nodiscard]] std::optional<domain::PlaybackError> failure() const override;

  private:
    friend class AudioStore;
    AudioStream(std::shared_ptr<AudioStreamState> state, QPointer<AudioStore> store, quint64 id);
    std::shared_ptr<AudioStreamState> m_state;
    std::unique_ptr<QFile> m_file; // the reader's own handle, used on the reading thread
    // This handle's own (the file may have other handles: the same song again, the next
    // one being the same file); under m_state->lock.
    bool m_canceled = false;
    bool m_interruptPending = false; // the next read that has to wait returns -1
    QPointer<AudioStore> m_store;
    quint64 m_id;
};

// The persistent audio cache with streaming download (docs/tasks/audio-engine.md §4.1).
// Files are keyed by the source's identity: a provider md5 plus size (content-addressed,
// checked against the finished file) or a stream name (stream-addressed, trusted once it
// decodes). A source without either streams through a temporary file that is never kept.
// Owner-thread object; AudioStream reads are thread-safe. Linux only for now: elsewhere, or
// when the root fails the private-path checks, nothing is kept (every file is temporary).
class AudioStore : public QObject
{
    Q_OBJECT
  public:
    struct Options {
        QString root; // ~/.cache/Cirrus/audio, 0700
        qint64 capacityBytes = 2LL * 1024 * 1024 * 1024;
        qint64 maximumFileBytes = 512LL * 1024 * 1024;
        qint64 chunkBytes = 256 * 1024;
        int retryDelayMs = 500;       // doubled per retry; tests shorten it
        int refreshTimeoutMs = 30000; // waiting for a new address
    };
    explicit AudioStore(Options options, std::unique_ptr<QNetworkAccessManager> manager = {},
                        QObject* parent = nullptr);
    ~AudioStore() override;

    // Opaque account partition (hashed into a directory name). Changing it fails every
    // open stream with StaleContext. Empty: nothing is kept.
    void setPartition(const QString& partition);
    [[nodiscard]] bool persistent() const { return !m_partitionDir.isEmpty(); }

    // A stream for `source`; null when the source is unusable (failure says why).
    std::shared_ptr<AudioStream> open(const domain::MediaSource& source,
                                      domain::PlaybackError* failure = nullptr);
    // Downloads the first `bytes` of `source` ahead of time (the next track).
    void prefetch(const domain::MediaSource& source, qint64 bytes);
    // A new signed address for the stream that asked for one (urlNeeded).
    void refresh(quint64 stream, QUrl url, QDeadlineTimer validUntil);

    [[nodiscard]] qint64 usage() const; // bytes on disk, all partitions
    // The limit for all partitions together; lowering it evicts the least recently
    // used files now (open ones stay).
    void setCapacity(qint64 bytes);
    [[nodiscard]] qint64 capacity() const { return m_options.capacityBytes; }
    void clear(bool allPartitions = false); // open streams' files go when they close

    // What automix learned of a kept file (an automix::Analysis, serialized), kept beside
    // it for as long as the file is: evicted and cleared with it. Empty when there is none
    // or `source` has no kept file.
    [[nodiscard]] QByteArray analysis(const domain::MediaSource& source) const;
    void keepAnalysis(const domain::MediaSource& source, const QByteArray& bytes);

  signals:
    // The address expired or the CDN refused it: refresh() within refreshTimeoutMs, or
    // the stream fails with Expired.
    void urlNeeded(quint64 stream);

  private:
    struct Download;
    friend class AudioStream;
    void release(quint64 id);
    void want(quint64 id);
    void schedule(Download& download);
    void request(Download& download, qint64 start, qint64 end);
    void consume(quint64 id);
    void finished(quint64 id);
    void retry(Download& download);
    void needUrl(Download& download);
    void fail(Download& download, domain::PlaybackError error);
    void completed(Download& download);
    void verified(QString base, qint64 size, QByteArray md5, bool good);
    void abortReply(Download& download);
    void retire(quint64 id);
    void saveMeta(const Download& download) const;
    Download* create(const domain::MediaSource& source, domain::PlaybackError* failure);
    void enforceCapacity();
    // partition directory + identity token and size, empty when the source can't be kept
    QString baseFor(const domain::MediaSource& source) const;
    [[nodiscard]] bool isOpenPath(const QString& path) const;
    struct Entry {
        QString data, meta, analysis;
        qint64 bytes = 0;
        qint64 lastUsed = 0;
    };
    [[nodiscard]] QList<Entry> entries(const QString& dir) const; // dir empty: every partition
    Download* findByBase(const QString& base);

    Options m_options;
    std::unique_ptr<QNetworkAccessManager> m_manager;
    bool m_rootUsable = false;
    QString m_partitionDir; // empty: nothing kept
    QHash<quint64, std::shared_ptr<Download>> m_downloads;
    quint64 m_nextId = 0;
};
} // namespace cirrus::media
