#pragma once
#include "infrastructure/media/audio_decoder.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace cirrus::media
{
class AudioDecoderTestAccess
{
  public:
    static void reader(AudioDecoder& decoder, int (*read)(AVFormatContext*, AVPacket*))
    {
        decoder.m_readFrame = read;
    }
    static int invalid(AVFormatContext*, AVPacket*) { return AVERROR_INVALIDDATA; }
    static int readSource(AVFormatContext* format, AVPacket*)
    {
        uint8_t buffer[1];
        return AudioDecoder::readPacket(format->pb->opaque, buffer, sizeof buffer);
    }
};
} // namespace cirrus::media
