#include "backend_sources.h"

#include "backend_catalog.h"
#include "backend_comments.h"
#include "backend_host.h"
#include "backend_images.h"
#include "backend_library.h"
#include "backend_playback.h"
#include "backend_session.h"
#include "backend_stats.h"
#include "backend_together.h"
#include "fake_sources.h"

#include "application/playback/listening_reporter.h"

#include <QPointer>

namespace cirrus::ui
{

Sources makeBackendSources(BackendHost& host)
{
    Sources sources = makeFakeSources();
    // Step 2: covers (sample covers still come from the fakes) and the cache.
    sources.images = std::make_unique<BackendImages>(host.runtime(), std::move(sources.images));
    sources.cache = std::make_unique<BackendCache>(host.runtime());
    // Step 3: the account, sign-in and restoring a remembered account.
    sources.session = std::make_unique<BackendSession>(host.runtime());
    // Steps 4, 5 and 8: search, details, home and discover.
    sources.catalog = std::make_unique<BackendCatalog>(host.runtime());
    // Step 9: comments.
    sources.comments = std::make_unique<BackendComments>(host.runtime());
    // Step 6: playback and lyrics.
    auto playback = std::make_unique<BackendPlayback>(host.runtime(), *sources.catalog);
    // The listening reports take each song's origin from it (integration.md §6.1).
    host.reporter().setOriginLookup(
        [adapter = QPointer<BackendPlayback>(playback.get())](
            QUuid entry) -> std::optional<application::ListeningReporter::Origin> {
            if (!adapter)
                return std::nullopt;
            const PlayOrigin origin = adapter->originOf(entry);
            using Source = domain::ListeningReportSource;
            switch (origin.kind) {
            case playback::FromPlaylist:
                return {{Source::List, origin.id}};
            case playback::FromAlbum:
                return {{Source::Album, origin.id}};
            case playback::FromArtist:
                return {{Source::Artist, origin.id}};
            case playback::FromChart:
                return {{Source::Toplist, origin.id}};
            case playback::FromDaily:
                return {{Source::DailyRecommend, origin.id}};
            case playback::FromSearch:
                return {{Source::Search, origin.id}};
            case playback::FromSong:
                return {{Source::Song, origin.id}};
            default:
                return std::nullopt;
            }
        });
    sources.playback = std::move(playback);
    sources.lyrics = std::make_unique<BackendLyrics>(host.runtime());
    // Step 7: the library and its writes.
    sources.library = std::make_unique<BackendLibrary>(host.runtime());
    // Local listening statistics (pages/stats.md).
    sources.stats = std::make_unique<BackendStats>(host.runtime());
    // Listen together (pages/together.md).
    sources.together = std::make_unique<BackendTogether>(host);
    return sources;
}

} // namespace cirrus::ui
