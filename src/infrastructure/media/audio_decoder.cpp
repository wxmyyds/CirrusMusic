#include "audio_decoder.h"
#include "byte_source.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <mutex>

namespace cirrus::media
{
using namespace domain;
namespace
{
constexpr int kIoBuffer = 64 * 1024;
constexpr qint64 kPrerollMs = 300; // decode this much before a seek target

// One accepted container/codec pair. The provider's `format` selects the demuxer, the codec
// whitelist and the expected decoder; FFmpeg never probes anything else. MP3 and FLAC keep
// their own demuxers; AAC and E-AC-3 arrive in an MP4/fMP4 container (the Bilibili CDN's
// audio/mp4), demuxed by "mov".
struct FormatRule {
    const char* input;   // av_find_input_format name, null when unsupported
    const char* codecs;  // avcodec whitelist
    const char* demuxer; // avformat whitelist
    AVCodecID expected;
};
std::optional<FormatRule> ruleFor(const QString& format)
{
    if (format == QStringLiteral("mp3"))
        return FormatRule{"mp3", "mp3float,mp3", "mp3", AV_CODEC_ID_MP3};
    if (format == QStringLiteral("flac"))
        return FormatRule{"flac", "flac", "flac", AV_CODEC_ID_FLAC};
    if (format == QStringLiteral("flac-mp4"))
        return FormatRule{"mov", "flac", "mov", AV_CODEC_ID_FLAC};
    if (format == QStringLiteral("aac"))
        return FormatRule{"mov", "aac", "mov", AV_CODEC_ID_AAC};
    if (format == QStringLiteral("eac3"))
        return FormatRule{"mov", "eac3", "mov", AV_CODEC_ID_EAC3};
    return std::nullopt;
}
} // namespace

AudioDecoder::~AudioDecoder()
{
    swr_free(&m_convert);
    av_frame_free(&m_frame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_codec);
    avformat_close_input(&m_format);
    if (m_io) {
        av_freep(&m_io->buffer);
        avio_context_free(&m_io);
    }
}

void AudioDecoder::failWith(PlaybackError error)
{
    if (!m_error)
        m_error = error;
    m_done = true;
}

int AudioDecoder::readPacket(void* opaque, uint8_t* buffer, int size)
{
    auto* self = static_cast<AudioDecoder*>(opaque);
    // FFmpeg reads again after a failed read; once interrupted, fail fast until resume().
    if (self->m_interrupted || self->m_error)
        return AVERROR_EXIT;
    const qint64 got = self->m_stream->read(self->m_offset, reinterpret_cast<char*>(buffer), size);
    if (got == 0)
        return AVERROR_EOF;
    if (got < 0) {
        // A failed download is the reason, not the decoder; otherwise it was interrupted.
        if (const auto failure = self->m_stream->failure())
            self->failWith(*failure);
        else
            self->m_interrupted = true;
        return AVERROR_EXIT;
    }
    self->m_offset += got;
    return int(got);
}

int64_t AudioDecoder::seekStream(void* opaque, int64_t offset, int whence)
{
    auto* self = static_cast<AudioDecoder*>(opaque);
    const qint64 size = self->m_stream->size();
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE:
        return size;
    case SEEK_SET:
        break;
    case SEEK_CUR:
        offset += self->m_offset;
        break;
    case SEEK_END:
        offset += size;
        break;
    default:
        return AVERROR(EINVAL);
    }
    if (offset < 0 || offset > size)
        return AVERROR(EINVAL);
    self->m_offset = offset;
    return offset;
}

bool AudioDecoder::open(std::shared_ptr<ByteSource> stream, const QString& format)
{
    // FFmpeg logs to stderr; its warnings are routine here (the MP3 gapless trim says
    // "Could not update timestamps for skipped samples" on every file).
    static std::once_flag quiet;
    std::call_once(quiet, [] { av_log_set_level(AV_LOG_ERROR); });
    m_stream = std::move(stream);
    const auto rule = ruleFor(format);
    const AVInputFormat* input = rule ? av_find_input_format(rule->input) : nullptr;
    const AVCodecID expected = rule ? rule->expected : AV_CODEC_ID_NONE;
    const AVCodec* codec = rule ? avcodec_find_decoder(expected) : nullptr;
    if (!rule || !input || !codec) {
        failWith(PlaybackError::UnsupportedFormat);
        return false;
    }
    auto* buffer = static_cast<uint8_t*>(av_malloc(kIoBuffer));
    m_io = avio_alloc_context(buffer, kIoBuffer, 0, this, &AudioDecoder::readPacket, nullptr,
                              &AudioDecoder::seekStream);
    m_format = avformat_alloc_context();
    if (!buffer || !m_io || !m_format) {
        failWith(PlaybackError::Storage);
        return false;
    }
    m_format->pb = m_io;
    m_format->flags |= AVFMT_FLAG_CUSTOM_IO;
    m_format->interrupt_callback = {[](void* opaque) {
                                        const auto* self = static_cast<AudioDecoder*>(opaque);
                                        return int(self->m_interrupted || self->m_error);
                                    },
                                    this};
    m_format->codec_whitelist = av_strdup(rule->codecs);
    m_format->format_whitelist = av_strdup(rule->demuxer);
    if (avformat_open_input(&m_format, nullptr, input, nullptr) < 0) {
        // avformat_open_input frees the context on failure.
        failWith(PlaybackError::UnsupportedFormat);
        return false;
    }
    if (avformat_find_stream_info(m_format, nullptr) < 0) {
        failWith(PlaybackError::Decode);
        return false;
    }
    m_streamIndex = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_streamIndex < 0 || m_format->streams[m_streamIndex]->codecpar->codec_id != expected) {
        failWith(PlaybackError::UnsupportedFormat);
        return false;
    }
    // Only the audio is read: cover art and anything else is dropped by the demuxer.
    for (unsigned i = 0; i < m_format->nb_streams; ++i)
        if (int(i) != m_streamIndex)
            m_format->streams[i]->discard = AVDISCARD_ALL;
    const AVStream* audio = m_format->streams[m_streamIndex];
    m_codec = avcodec_alloc_context3(codec);
    if (!m_codec || avcodec_parameters_to_context(m_codec, audio->codecpar) < 0 ||
        avcodec_open2(m_codec, codec, nullptr) < 0 || m_codec->sample_rate <= 0 ||
        m_codec->sample_rate > 768000 || m_codec->ch_layout.nb_channels <= 0 ||
        m_codec->ch_layout.nb_channels > 8) {
        failWith(PlaybackError::Decode);
        return false;
    }
    m_rate = m_codec->sample_rate;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&m_convert, &stereo, AV_SAMPLE_FMT_FLT, m_rate, &m_codec->ch_layout,
                            m_codec->sample_fmt, m_rate, 0, nullptr) < 0 ||
        swr_init(m_convert) < 0) {
        failWith(PlaybackError::Decode);
        return false;
    }
    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    if (!m_packet || !m_frame) {
        failWith(PlaybackError::Storage);
        return false;
    }
    if (audio->duration != AV_NOPTS_VALUE)
        m_durationMs = av_rescale_q(audio->duration, audio->time_base, {1, 1000});
    else if (m_format->duration != AV_NOPTS_VALUE)
        m_durationMs = m_format->duration / 1000;
    if (m_durationMs <= 0) {
        failWith(PlaybackError::Decode);
        return false;
    }
    return true;
}

void AudioDecoder::convert(const AVFrame* frame, qint64 skip)
{
    const int count = swr_get_out_samples(m_convert, frame->nb_samples);
    if (count <= 0)
        return;
    const size_t at = m_pending.size();
    m_pending.resize(at + size_t(count) * 2);
    auto* out = reinterpret_cast<uint8_t*>(m_pending.data() + at);
    const int made =
        swr_convert(m_convert, &out, count, const_cast<const uint8_t**>(frame->extended_data),
                    frame->nb_samples);
    m_pending.resize(at + size_t(std::max(made, 0)) * 2);
    if (skip > 0) {
        const auto drop = std::min<size_t>(size_t(skip) * 2, m_pending.size() - at);
        m_pending.erase(m_pending.begin() + qsizetype(at),
                        m_pending.begin() + qsizetype(at + drop));
    }
}

bool AudioDecoder::decodeSome()
{
    while (!m_error && !m_interrupted) {
        const int got = avcodec_receive_frame(m_codec, m_frame);
        if (got == 0) {
            qint64 skip = 0;
            if (m_dropUntil >= 0) {
                const AVStream* stream = m_format->streams[m_streamIndex];
                const qint64 start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
                const qint64 pts = m_frame->best_effort_timestamp;
                if (pts != AV_NOPTS_VALUE) {
                    const qint64 first = av_rescale_q(pts - start, stream->time_base, {1, m_rate});
                    skip = m_dropUntil - first;
                    if (skip >= m_frame->nb_samples) {
                        av_frame_unref(m_frame);
                        continue;
                    }
                }
                m_dropUntil = -1;
            }
            convert(m_frame, skip);
            av_frame_unref(m_frame);
            return true;
        }
        if (got == AVERROR_EOF) {
            m_done = true;
            return false;
        }
        if (got != AVERROR(EAGAIN)) {
            failWith(PlaybackError::Decode);
            return false;
        }
        if (m_inputDone) {
            m_done = true;
            return false;
        }
        const int read = av_read_frame(m_format, m_packet);
        if (m_error || m_interrupted)
            return false; // the stream failed or was interrupted under the demuxer
        if (read < 0) {
            m_inputDone = true;
            avcodec_send_packet(m_codec, nullptr); // drain
            continue;
        }
        if (m_packet->stream_index == m_streamIndex) {
            const int sent = avcodec_send_packet(m_codec, m_packet);
            // A damaged frame is skipped, as players do; anything else is fatal.
            if (sent < 0 && sent != AVERROR_INVALIDDATA && sent != AVERROR(EAGAIN)) {
                av_packet_unref(m_packet);
                failWith(PlaybackError::Decode);
                return false;
            }
        }
        av_packet_unref(m_packet);
    }
    return false;
}

bool AudioDecoder::read(std::vector<float>& out, qsizetype frames)
{
    while (qsizetype(m_pending.size() / 2) < frames && !m_done && !m_error && !m_interrupted) {
        if (!decodeSome())
            break;
    }
    const qsizetype take = std::min<qsizetype>(frames, qsizetype(m_pending.size() / 2));
    out.insert(out.end(), m_pending.begin(), m_pending.begin() + take * 2);
    m_pending.erase(m_pending.begin(), m_pending.begin() + take * 2);
    m_position += take;
    return take > 0;
}

void AudioDecoder::resume()
{
    m_interrupted = false;
    if (m_io) {
        m_io->error = 0; // the interrupted read left its mark on the I/O context
        m_io->eof_reached = 0;
    }
}

bool AudioDecoder::seek(qint64 ms)
{
    if (m_error && *m_error != PlaybackError::Decode)
        return false; // a failed download can't seek
    m_error.reset();
    resume();
    const AVStream* stream = m_format->streams[m_streamIndex];
    const qint64 start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const qint64 from = std::max<qint64>(0, ms - kPrerollMs);
    const qint64 ts = start + av_rescale_q(from, {1, 1000}, stream->time_base);
    // Past what has been read (its index), FFmpeg's MP3 seek reads every frame up to
    // there: the download would have to get there in order, and on a slow line that took
    // long (2026-11-22). A fast seek jumps there by the Xing table or the bit rate; within
    // the index, the index is exact.
    const int entries = avformat_index_get_entries_count(stream);
    const AVIndexEntry* last =
        entries > 0 ? avformat_index_get_entry(m_format->streams[m_streamIndex], entries - 1)
                    : nullptr;
    const bool past = !last || ts > last->timestamp;
    if (past)
        m_format->flags |= AVFMT_FLAG_FAST_SEEK;
    const int sought = av_seek_frame(m_format, m_streamIndex, ts, AVSEEK_FLAG_BACKWARD);
    m_format->flags &= ~AVFMT_FLAG_FAST_SEEK;
    if (sought < 0 || m_interrupted)
        return false;
    avcodec_flush_buffers(m_codec);
    swr_init(m_convert); // drops what it held
    m_pending.clear();
    m_inputDone = false;
    m_done = false;
    m_dropUntil = av_rescale(ms, m_rate, 1000);
    m_position = m_dropUntil;
    return true;
}

AudioResampler::AudioResampler(int inRate, int outRate) : m_in(inRate), m_out(outRate)
{
    if (inRate == outRate)
        return;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&m_swr, &stereo, AV_SAMPLE_FMT_FLT, outRate, &stereo, AV_SAMPLE_FMT_FLT,
                            inRate, 0, nullptr) < 0 ||
        swr_init(m_swr) < 0)
        swr_free(&m_swr);
}
AudioResampler::~AudioResampler() { swr_free(&m_swr); }

void AudioResampler::process(const std::vector<float>& in, std::vector<float>& out)
{
    if (m_in == m_out) {
        out.insert(out.end(), in.begin(), in.end());
        return;
    }
    if (!m_swr)
        return;
    const int frames = int(in.size() / 2);
    const int room = swr_get_out_samples(m_swr, frames) + 64;
    const size_t at = out.size();
    out.resize(at + size_t(room) * 2);
    auto* dst = reinterpret_cast<uint8_t*>(out.data() + at);
    const auto* src = reinterpret_cast<const uint8_t*>(in.data());
    const int made = swr_convert(m_swr, &dst, room, &src, frames);
    out.resize(at + size_t(std::max(made, 0)) * 2);
}

void AudioResampler::flush(std::vector<float>& out)
{
    if (!m_swr)
        return;
    const int room = swr_get_out_samples(m_swr, 0) + 64;
    const size_t at = out.size();
    out.resize(at + size_t(room) * 2);
    auto* dst = reinterpret_cast<uint8_t*>(out.data() + at);
    const int made = swr_convert(m_swr, &dst, room, nullptr, 0);
    out.resize(at + size_t(std::max(made, 0)) * 2);
}
} // namespace cirrus::media
