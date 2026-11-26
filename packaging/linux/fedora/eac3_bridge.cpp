#include "eac3_bridge.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}
#include <cstring>
#include <limits>
#include <new>

struct CirrusEac3 {
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* convert = nullptr;
    uint8_t* pcm = nullptr;
    unsigned capacity = 0;
    int rate = 0;
};

namespace
{
int resultFor(int result)
{
    if (result >= 0)
        return CIRRUS_EAC3_FRAME;
    if (result == AVERROR(EAGAIN))
        return CIRRUS_EAC3_NEED_INPUT;
    if (result == AVERROR_EOF)
        return CIRRUS_EAC3_END;
    if (result == AVERROR_INVALIDDATA)
        return CIRRUS_EAC3_INVALID_DATA;
    return CIRRUS_EAC3_ERROR;
}
} // namespace

CirrusEac3* cirrus_eac3_create(const CirrusEac3Config* config)
{
    if (!config || config->sample_rate <= 0 || config->sample_rate > 768000 ||
        config->channels <= 0 || config->channels > 8 || config->time_base_num <= 0 ||
        config->time_base_den <= 0 || config->extradata_size < 0 ||
        config->extradata_size > 1024 * 1024 || (config->extradata_size > 0 && !config->extradata))
        return nullptr;
    av_log_set_level(AV_LOG_ERROR);
    // AC3 is an upstream EAC3 build dependency, but is never exposed by this bridge.
    const auto* codec = avcodec_find_decoder(AV_CODEC_ID_EAC3);
    auto* decoder = new (std::nothrow) CirrusEac3;
    if (!decoder)
        return nullptr;
    decoder->codec = avcodec_alloc_context3(codec);
    if (!codec || !decoder->codec) {
        cirrus_eac3_destroy(decoder);
        return nullptr;
    }
    auto* context = decoder->codec;
    context->sample_rate = config->sample_rate;
    av_channel_layout_default(&context->ch_layout, config->channels);
    context->pkt_timebase = {config->time_base_num, config->time_base_den};
    if (config->extradata_size > 0) {
        context->extradata = static_cast<uint8_t*>(
            av_mallocz(size_t(config->extradata_size) + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!context->extradata) {
            cirrus_eac3_destroy(decoder);
            return nullptr;
        }
        std::memcpy(context->extradata, config->extradata, size_t(config->extradata_size));
        context->extradata_size = config->extradata_size;
    }
    decoder->packet = av_packet_alloc();
    decoder->frame = av_frame_alloc();
    if (avcodec_open2(context, codec, nullptr) < 0 || !decoder->packet || !decoder->frame) {
        cirrus_eac3_destroy(decoder);
        return nullptr;
    }
    decoder->rate = context->sample_rate;
    return decoder;
}

void cirrus_eac3_destroy(CirrusEac3* decoder)
{
    if (!decoder)
        return;
    swr_free(&decoder->convert);
    av_frame_free(&decoder->frame);
    av_packet_free(&decoder->packet);
    avcodec_free_context(&decoder->codec);
    av_freep(&decoder->pcm);
    delete decoder;
}

int cirrus_eac3_sample_rate(const CirrusEac3* decoder) { return decoder ? decoder->rate : 0; }

int cirrus_eac3_send(CirrusEac3* decoder, const uint8_t* data, int size, int64_t pts, int64_t dts,
                     int64_t duration)
{
    if (!decoder || size < 0 || (size > 0 && !data))
        return CIRRUS_EAC3_ERROR;
    if (!data && size == 0)
        return resultFor(avcodec_send_packet(decoder->codec, nullptr));
    av_packet_unref(decoder->packet);
    if (av_new_packet(decoder->packet, size) < 0)
        return CIRRUS_EAC3_ERROR;
    std::memcpy(decoder->packet->data, data, size_t(size));
    decoder->packet->pts = pts;
    decoder->packet->dts = dts;
    decoder->packet->duration = duration;
    const int result = avcodec_send_packet(decoder->codec, decoder->packet);
    av_packet_unref(decoder->packet);
    return resultFor(result);
}

int cirrus_eac3_receive(CirrusEac3* decoder, const float** pcm, int* frames, int64_t* pts)
{
    if (!decoder || !pcm || !frames || !pts)
        return CIRRUS_EAC3_ERROR;
    *pcm = nullptr;
    *frames = 0;
    *pts = INT64_MIN;
    const int received = avcodec_receive_frame(decoder->codec, decoder->frame);
    if (received < 0)
        return resultFor(received);
    const auto fail = [decoder] {
        av_frame_unref(decoder->frame);
        return CIRRUS_EAC3_ERROR;
    };
    const auto* frame = decoder->frame;
    if (frame->sample_rate != decoder->rate || frame->ch_layout.nb_channels <= 0 ||
        frame->ch_layout.nb_channels > 8)
        return fail();
    if (!decoder->convert) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&decoder->convert, &stereo, AV_SAMPLE_FMT_FLT, decoder->rate,
                                &frame->ch_layout, AVSampleFormat(frame->format), decoder->rate, 0,
                                nullptr) < 0 ||
            swr_init(decoder->convert) < 0)
            return fail();
    }
    const int count = swr_get_out_samples(decoder->convert, frame->nb_samples);
    if (count < 0 || size_t(count) > std::numeric_limits<unsigned>::max() / (2 * sizeof(float)))
        return fail();
    av_fast_malloc(&decoder->pcm, &decoder->capacity, size_t(count) * 2 * sizeof(float));
    if (!decoder->pcm)
        return fail();
    const int made =
        swr_convert(decoder->convert, &decoder->pcm, count,
                    const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
    if (made < 0)
        return fail();
    *pcm = reinterpret_cast<const float*>(decoder->pcm);
    *frames = made;
    *pts = frame->best_effort_timestamp;
    av_frame_unref(decoder->frame);
    return CIRRUS_EAC3_FRAME;
}

void cirrus_eac3_reset(CirrusEac3* decoder)
{
    if (!decoder)
        return;
    avcodec_flush_buffers(decoder->codec);
    swr_free(&decoder->convert);
    av_frame_unref(decoder->frame);
    av_packet_unref(decoder->packet);
}
