#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CIRRUS_EAC3_API __attribute__((visibility("default")))

typedef struct CirrusEac3 CirrusEac3;
enum CirrusEac3Status {
    CIRRUS_EAC3_FRAME = 0,
    CIRRUS_EAC3_NEED_INPUT = 1,
    CIRRUS_EAC3_END = 2,
    CIRRUS_EAC3_INVALID_DATA = 3,
    CIRRUS_EAC3_ERROR = 4,
    CIRRUS_EAC3_CORRUPT_FRAME = 5
};
typedef struct CirrusEac3Config {
    int sample_rate;
    int channels;
    int time_base_num;
    int time_base_den;
    const uint8_t* extradata;
    int extradata_size;
} CirrusEac3Config;

// No FFmpeg object crosses this ABI. Inputs are copied. PCM remains owned by the handle
// until the next receive/reset/destroy; the caller copies it and never frees it.
// Packet and PCM timestamps share the configured time base; INT64_MIN means absent.
CIRRUS_EAC3_API CirrusEac3* cirrus_eac3_create(const CirrusEac3Config* config);
CIRRUS_EAC3_API void cirrus_eac3_destroy(CirrusEac3* decoder);
CIRRUS_EAC3_API int cirrus_eac3_sample_rate(const CirrusEac3* decoder);
// null data and zero size drain; every nonempty packet has its own PTS/DTS/duration.
CIRRUS_EAC3_API int cirrus_eac3_send(CirrusEac3* decoder, const uint8_t* data, int size,
                                     int64_t pts, int64_t dts, int64_t duration);
CIRRUS_EAC3_API int cirrus_eac3_receive(CirrusEac3* decoder, const float** pcm, int* frames,
                                        int64_t* pts);
CIRRUS_EAC3_API void cirrus_eac3_reset(CirrusEac3* decoder);
#ifdef __cplusplus
}
#endif
