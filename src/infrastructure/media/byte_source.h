#pragma once
#include "domain/playback/playback_types.h"
#include <QFile>
#include <QString>
#include <memory>
#include <mutex>
#include <optional>

namespace cirrus::media
{

// The bytes of one media file, for the decoder: the streaming cache (AudioStream) or a
// plain local file (LocalFileSource: the automix tool and tests). Reads may block.
class ByteSource
{
  public:
    virtual ~ByteSource() = default;
    [[nodiscard]] virtual qint64 size() const = 0;
    // Up to `maximum` bytes at `position`: > 0 read, 0 at the end, -1 failed or interrupted.
    virtual qint64 read(qint64 position, char* out, qint64 maximum) = 0;
    // Decoder evidence, reported on its reading thread. Success means an uninterrupted
    // linear decode from the start through demux EOF and codec drain, with audio frames.
    virtual void decoded() {}
    virtual void decodeFailed(domain::PlaybackError) {}
    // Why reads fail for good; nullopt while they work (or were only interrupted).
    [[nodiscard]] virtual std::optional<domain::PlaybackError> failure() const = 0;
};

// A whole file on disk, read on the caller's thread. Not for played sources: those go
// through the AudioStore and its private-path rules.
class LocalFileSource final : public ByteSource
{
  public:
    // Null when the file can't be opened.
    [[nodiscard]] static std::shared_ptr<LocalFileSource> open(const QString& path)
    {
        auto source = std::shared_ptr<LocalFileSource>(new LocalFileSource(path));
        return source->m_file.open(QIODevice::ReadOnly) ? source : nullptr;
    }
    [[nodiscard]] qint64 size() const override { return m_file.size(); }
    qint64 read(qint64 position, char* out, qint64 maximum) override
    {
        std::lock_guard guard(m_lock);
        if (!m_file.seek(position))
            return -1;
        return m_file.read(out, maximum);
    }
    [[nodiscard]] std::optional<domain::PlaybackError> failure() const override
    {
        return std::nullopt;
    }

  private:
    explicit LocalFileSource(const QString& path) : m_file(path) {}
    std::mutex m_lock;
    QFile m_file;
};

} // namespace cirrus::media
