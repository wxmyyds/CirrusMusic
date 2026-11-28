#include "fedora_eac3_codec.h"
#include "eac3_bridge.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
}

namespace cirrus::media
{
namespace
{
int resultFor(int status)
{
    switch (status) {
    case CIRRUS_EAC3_FRAME:
    case CIRRUS_EAC3_CORRUPT_FRAME:
        return 0;
    case CIRRUS_EAC3_NEED_INPUT:
        return AVERROR(EAGAIN);
    case CIRRUS_EAC3_END:
        return AVERROR_EOF;
    case CIRRUS_EAC3_INVALID_DATA:
        return AVERROR_INVALIDDATA;
    default:
        return AVERROR_EXTERNAL;
    }
}
} // namespace

std::unique_ptr<FedoraEac3Decoder> FedoraEac3Decoder::create(const AVCodecParameters* parameters,
                                                             int timeBaseNum, int timeBaseDen)
{
    if (!parameters || parameters->codec_id != AV_CODEC_ID_EAC3)
        return {};
    const CirrusEac3Config config{
        parameters->sample_rate, parameters->ch_layout.nb_channels, timeBaseNum, timeBaseDen,
        parameters->extradata,   parameters->extradata_size};
    auto decoder = std::unique_ptr<FedoraEac3Decoder>(new FedoraEac3Decoder);
    decoder->m_decoder = cirrus_eac3_create(&config);
    if (!decoder->m_decoder)
        return {};
    return decoder;
}
FedoraEac3Decoder::~FedoraEac3Decoder() { cirrus_eac3_destroy(m_decoder); }
int FedoraEac3Decoder::sampleRate() const { return cirrus_eac3_sample_rate(m_decoder); }
int FedoraEac3Decoder::send(const AVPacket* packet)
{
    if (!packet)
        return resultFor(cirrus_eac3_send(m_decoder, nullptr, 0, INT64_MIN, INT64_MIN, 0));
    return resultFor(cirrus_eac3_send(m_decoder, packet->data, packet->size, packet->pts,
                                      packet->dts, packet->duration));
}
int FedoraEac3Decoder::receive()
{
    const float* pcm = nullptr;
    int frames = 0;
    int64_t pts = INT64_MIN;
    m_frameCorrupt = false;
    const int status = cirrus_eac3_receive(m_decoder, &pcm, &frames, &pts);
    m_frameCorrupt = status == CIRRUS_EAC3_CORRUPT_FRAME;
    const int result = resultFor(status);
    if (result == 0) {
        if (frames < 0 || (frames > 0 && !pcm))
            return AVERROR_EXTERNAL;
        m_samples.clear();
        if (frames > 0)
            m_samples.assign(pcm, pcm + size_t(frames) * 2);
        m_pts = pts;
    }
    return result;
}
void FedoraEac3Decoder::flush()
{
    cirrus_eac3_reset(m_decoder);
    m_samples.clear();
    m_pts = INT64_MIN;
    m_frameCorrupt = false;
}
} // namespace cirrus::media
