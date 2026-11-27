#pragma once
#include "listening_types.h"
#include "logging/log.h"

namespace cirrus::domain
{
enum class ListeningEnd { Active, Ended, Failed, Stopped, Replaced, Detached };
CIRRUS_LOG_ENUM(ListeningEnd, Active, Ended, Failed, Stopped, Replaced, Detached)
struct ListeningRecord {
    LoadToken token;   // distinct loads, including replaying the same song/queue entry
    QString accountId; // empty for guest; never reassigned on login
    SongId song;
    ListeningMode source = ListeningMode::Queue;
    QUuid queueEntry;  // no playlist/album origin is fabricated
    PlayOrigin origin; // the queue entry's, as the caller gave it; None for FM
    ProgramId program; // the queue entry's: a podcast program played through `song`
    QString alg;       // the queue entry's 心动模式 mark, for the reports; never stored
    std::optional<PreviewRange> preview;
    QString actualQuality;
    qint64 playedMs = 0; // observed elapsed progress, NOT final position or unique coverage
    bool capped = false;
    ListeningEnd end = ListeningEnd::Active;
    QUuid recordId;         // independent local row identity, not the private load token
    qint64 startedAtMs = 0; // UTC calendar metadata; NEVER a duration source
    std::optional<qint64> endedAtMs;
    SourceProvider provider = SourceProvider::Netease;
};
} // namespace cirrus::domain
