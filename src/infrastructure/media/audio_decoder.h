#pragma once
#include "domain/playback/playback_types.h"
#include <QString>
#include <memory>
#include <optional>
#include <vector>

struct AVFormatContext;
struct AVIOContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

namespace cirrus::media
{
class ByteSource;

// One audio stream decoded to interleaved stereo float at its own sample rate, read through
// a ByteSource (the AudioStore's stream; blocking: run it on a decoder thread). Only the
// demuxer and codec named by the provider's format are allowed: FFmpeg never probes other
// formats. MP3 and FLAC keep their own demuxers; "aac" and "eac3" are demuxed from MP4/fMP4
// (the provider's audio/mp4). Start/end padding (MP3 encoder delay, LAME header) is trimmed by
// libavcodec.
class AudioDecoder final
{
  public:
    AudioDecoder() = default;
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    // `format`: "mp3", "flac", "aac" (MP4/AAC) or "eac3" (MP4/E-AC-3). False on failure;
    // error() says why.
    bool open(std::shared_ptr<ByteSource> stream, const QString& format);
    [[nodiscard]] int sampleRate() const { return m_rate; }
    [[nodiscard]] qint64 durationMs() const { return m_durationMs; }
    // Position of the next frame read() gives, in source frames.
    [[nodiscard]] qint64 position() const { return m_position; }
    // Appends up to `frames` frames (2 floats each). False at the end or on failure.
    bool read(std::vector<float>& out, qsizetype frames);
    // Sample-exact: seeks a little earlier, then drops frames up to `ms`. False when it
    // fails, or when a read under it was interrupted (interrupted(); FFmpeg may call that
    // seek a success while it never got where it went).
    bool seek(qint64 ms);
    [[nodiscard]] bool atEnd() const { return m_done && m_pending.empty(); }
    [[nodiscard]] std::optional<domain::PlaybackError> error() const { return m_error; }
    // A read was interrupted (AudioStream::interrupt): not an error. seek(), or resume()
    // to go on where it was.
    [[nodiscard]] bool interrupted() const { return m_interrupted; }
    void resume();

  private:
    static int readPacket(void* opaque, uint8_t* buffer, int size);
    static int64_t seekStream(void* opaque, int64_t offset, int whence);
    bool decodeSome();
    void convert(const AVFrame* frame, qint64 skip);
    void failWith(domain::PlaybackError error);

    std::shared_ptr<ByteSource> m_stream;
    qint64 m_offset = 0; // byte position in the stream
    AVIOContext* m_io = nullptr;
    AVFormatContext* m_format = nullptr;
    AVCodecContext* m_codec = nullptr;
    SwrContext* m_convert = nullptr;
    AVPacket* m_packet = nullptr;
    AVFrame* m_frame = nullptr;
    int m_streamIndex = -1;
    int m_rate = 0;
    qint64 m_durationMs = 0;
    qint64 m_position = 0;
    qint64 m_dropUntil = -1;
    std::vector<float> m_pending;
    bool m_inputDone = false;
    bool m_done = false;
    bool m_interrupted = false;
    std::optional<domain::PlaybackError> m_error;
};

// Interleaved stereo float from one rate to another. Kept across tracks of the same rate
// so their joint stays continuous (gapless); flush() only when the rate changes or at
// the very end.
class AudioResampler final
{
  public:
    AudioResampler(int inRate, int outRate);
    ~AudioResampler();
    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;
    [[nodiscard]] int inRate() const { return m_in; }
    [[nodiscard]] int outRate() const { return m_out; }
    void process(const std::vector<float>& in, std::vector<float>& out);
    void flush(std::vector<float>& out);

  private:
    SwrContext* m_swr = nullptr; // null when the rates match
    int m_in, m_out;
};
} // namespace cirrus::media
