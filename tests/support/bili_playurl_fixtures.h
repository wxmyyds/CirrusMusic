#pragma once

#include <QByteArray>

namespace cirrus::test
{
inline const QByteArray kDashDolbyFlac = R"({
            "timelength": 200000,
            "dash": {
                "duration": 200,
                "video": [{"id": 80, "baseUrl": "https://v.bilivideo.com/v"}],
                "audio": [
                    {"id": 30280, "baseUrl": "https://a.bilivideo.com/a1",
                     "backupUrl": ["https://b.bilivideo.com/a1"],
                     "bandwidth": 192000, "mimeType": "audio/mp4", "codecs": "mp4a.40.2",
                     "size": 4800000, "md5": "0123456789abcdef0123456789abcdef"}
                ],
                "dolby": {"type": 1, "audio": [
                    {"id": 30250, "baseUrl": "https://a.bilivideo.com/d",
                     "bandwidth": 448000, "mimeType": "audio/eac3", "codecs": "ec-3"}
                ]},
                "flac": {"display": true, "audio":
                    {"id": 30251, "baseUrl": "https://a.bilivideo.com/f",
                     "bandwidth": 1000000, "mimeType": "audio/flac", "codecs": "fLaC"}
                }
            }
        })";

inline const QByteArray kPrioritizedDash = R"({
            "dash": {"video": [{"id": 80, "baseUrl": "https://v.bilivideo.com/v"}],
                     "audio": [{"id": 30280, "baseUrl": "https://cn.bilivideo.com/a",
                                "backupUrl": ["https://upos-sz.bilivideo.com/a"],
                                "bandwidth": 192000, "mimeType": "audio/mp4"}]}
        })";

inline const QByteArray kProgressive = R"({
            "timelength": 100000,
            "durl": [{"order": 1, "length": 100000, "size": 1250000,
                      "url": "https://a.bilivideo.com/p.mp4"}]
        })";

inline const QByteArray kEmptyAudio = R"({
            "dash": {"video": [{"id": 80, "baseUrl": "https://v.bilivideo.com/v"}], "audio": []}
        })";

} // namespace cirrus::test
