#include "backend_facade.h"
#include "comment_write_requests.h"

#include <QThread>
#include <QTimer>
#include <cmath>
#include <utility>

namespace cirrus::application
{

BackendFacade::BackendFacade(QObject* parent)
    : QObject(parent), m_commentWrites(std::make_unique<detail::CommentWriteRequests>())
{
}

BackendFacade::~BackendFacade() = default;

void BackendFacade::setState(BackendState state)
{
    m_state = state;
    emit stateChanged(state);
}

void BackendFacade::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Dormant)
        return;
    setState(BackendState::Starting);
    if (m_state == BackendState::Starting)
        emit startRequested();
}

void BackendFacade::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == BackendState::Stopping || m_state == BackendState::Stopped)
        return;
    ++m_generation;
    m_snapshot = {};
    invalidateMusic(MusicError::Canceled);
    invalidateListeningReports(domain::ReportError::Canceled);
    invalidateCollections(domain::CollectionError::Canceled);
    invalidateCommentWrites(domain::CommentWriteError::Canceled);
    invalidateTogether(domain::TogetherError::Canceled);
    invalidatePlayback();
    const auto pending = std::exchange(m_pending, {});
    setState(BackendState::Stopping);
    emit closeRequested();
    for (auto id : pending)
        emit acknowledged(id, false);
    QTimer::singleShot(5000, this, [this] {
        if (m_state == BackendState::Stopping)
            emit closeDelayed();
    });
}

quint64 BackendFacade::submit(AccountAction action, QrLoginOptions options,
                              domain::CredentialLogin credential, domain::SmsCodeRequest sms)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || m_pending.size() >= 32 || !options.isValid())
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id);
    // Polling and sending an SMS code leave the session (and everything on it) alone.
    if (action != AccountAction::Poll && action != AccountAction::SendSmsCode) {
        ++m_generation;
        m_snapshot = {}; // immediate intent barrier, before backend processes the command
        invalidateMusic(MusicError::StaleContext);
        invalidateListeningReports(domain::ReportError::StaleContext);
        invalidateCollections(domain::CollectionError::StaleContext);
        invalidateCommentWrites(domain::CommentWriteError::StaleContext);
        invalidateTogether(domain::TogetherError::StaleContext);
        invalidatePlayback();
    }
    emit commandRequested(
        {id, m_generation, action, options, std::move(credential), std::move(sms)});
    return id;
}

quint64 BackendFacade::beginLogin(QrLoginOptions options)
{
    return submit(AccountAction::BeginLogin, options);
}
quint64 BackendFacade::beginCredentialLogin(domain::CredentialLogin login, QrLoginOptions options)
{
    if (!domain::validCredentialLogin(login))
        return 0;
    return submit(AccountAction::BeginCredentialLogin, options, std::move(login));
}
quint64 BackendFacade::sendSmsCode(domain::SmsCodeRequest request)
{
    if (!domain::validSmsCodeRequest(request))
        return 0;
    return submit(AccountAction::SendSmsCode, {}, {}, std::move(request));
}
quint64 BackendFacade::restore() { return submit(AccountAction::Restore); }
quint64 BackendFacade::poll() { return submit(AccountAction::Poll); }
quint64 BackendFacade::cancel() { return submit(AccountAction::Cancel); }
quint64 BackendFacade::signOut() { return submit(AccountAction::SignOut); }

void BackendFacade::deliver(BackendEvent event)
{
    Q_ASSERT(QThread::currentThread() == thread());
    using Kind = BackendEvent::Kind;
    if (event.kind == Kind::Acknowledged) {
        if (m_pending.remove(event.commandId))
            emit acknowledged(event.commandId, event.accepted);
        return;
    }
    if (event.kind == Kind::StorageWarning) {
        if (m_state != BackendState::Stopped)
            emit persistenceWarning(event.persistenceArea);
        return;
    }
    if (event.kind == Kind::Saved) {
        if (m_state != BackendState::Stopped)
            emit savedSessionCompleted(event.session.token, event.saved);
        return; // storage outcomes retain their original token, never change session state
    }
    if (m_state == BackendState::Stopping || m_state == BackendState::Stopped ||
        event.generation != m_generation)
        return;
    if (event.kind == Kind::Started && m_state == BackendState::Starting) {
        const bool fatal = event.startupError == StartupError::Configuration ||
                           event.startupError == StartupError::Storage;
        setState(fatal ? BackendState::Failed : BackendState::Ready);
        if (m_state == BackendState::Ready || m_state == BackendState::Failed)
            emit started(event.startupError);
        return;
    }
    if (m_state != BackendState::Ready)
        return;
    if (event.kind == Kind::Session) {
        if (m_snapshot.token != event.session.token) {
            invalidateMusic(MusicError::StaleContext);
            invalidatePlayback();
        } else if (m_snapshot.accountId != event.session.accountId ||
                   m_snapshot.phase != event.session.phase) {
            for (auto id : m_music.keys())
                if (requiresAuthenticatedMusicQuery(m_music.value(id).query))
                    cancelMusicWith(id, MusicError::StaleContext);
        }
        if (m_snapshot.token != event.session.token ||
            m_snapshot.accountId != event.session.accountId ||
            event.session.phase != domain::SessionPhase::Authenticated) {
            invalidateListeningReports(domain::ReportError::StaleContext);
            invalidateCollections(domain::CollectionError::StaleContext);
            invalidateCommentWrites(domain::CommentWriteError::StaleContext);
            invalidateTogether(domain::TogetherError::StaleContext);
        }
        m_snapshot = event.session;
        emit sessionChanged(m_snapshot);
        return;
    }
    if (event.session.token != m_snapshot.token)
        return;
    switch (event.kind) {
    case Kind::Challenge:
        emit challengeReady(event.session.token, event.challenge);
        break;
    case Kind::Status:
        emit statusChanged(event.session.token, event.status);
        break;
    case Kind::SignedIn:
        emit signedIn(event.session.token, event.identity);
        break;
    case Kind::AuthFailed:
        emit failed(event.session.token, event.failure);
        break;
    case Kind::SmsCode:
        emit smsCodeSent(event.session.token, event.smsCode);
        break;
    default:
        break;
    }
}

quint64 BackendFacade::searchSongs(domain::SongSearch query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::songDetails(QList<domain::SongId> songs)
{
    return submitMusic(DetailQuery{std::move(songs)});
}
quint64 BackendFacade::lyrics(domain::SongId song)
{
    return submitMusic(LyricQuery{std::move(song)});
}

std::optional<domain::PlaybackSourceSelection>
BackendFacade::playbackSource(quint64 generation) const
{
    const auto found = m_playbackSources.constFind(generation);
    return found == m_playbackSources.cend() ? std::nullopt
                                            : std::optional<domain::PlaybackSourceSelection>(*found);
}

void BackendFacade::deliverPlaybackSource(domain::PlaybackSourceSelection selection)
{
    if (m_state != BackendState::Ready || selection.token.session != m_snapshot.token ||
        (selection.token.generation != m_playbackGeneration &&
         selection.token.generation != m_preloadGeneration) ||
        !PlaybackLyricQuery{selection}.isValid() ||
        (selection.token.generation == m_playbackGeneration && selection.song != m_playback.song) ||
        (selection.token.generation == m_preloadGeneration && selection.song != m_preloadSong))
        return;
    m_playbackSources.insert(selection.token.generation, selection);
    emit playbackSourceSelected(std::move(selection));
}

quint64 BackendFacade::playbackLyricGeneration(const domain::SongId& song) const
{
    if (m_state != BackendState::Ready)
        return 0;
    if (song == m_playback.song)
        return m_playbackGeneration;
    return m_preloadGeneration && song == m_preloadSong ? m_preloadGeneration : 0;
}

quint64 BackendFacade::playbackLyrics(quint64 generation)
{
    const auto source = playbackSource(generation);
    return source ? submitMusic(PlaybackLyricQuery{*source}) : 0;
}

quint64 BackendFacade::albumDetails(domain::AlbumId album)
{
    return submitMusic(domain::AlbumQuery{std::move(album)});
}
quint64 BackendFacade::artistDetails(domain::ArtistId artist)
{
    return submitMusic(domain::ArtistQuery{std::move(artist)});
}
quint64 BackendFacade::artistIntroduction(domain::ArtistId artist)
{
    return submitMusic(domain::ArtistIntroductionQuery{std::move(artist)});
}
quint64 BackendFacade::songWikiSummary(domain::SongId song)
{
    return submitMusic(domain::SongWikiQuery{std::move(song)});
}
quint64 BackendFacade::searchSuggestions(QString keywords)
{
    return submitMusic(domain::SearchSuggestionsQuery{std::move(keywords)});
}
quint64 BackendFacade::hotKeywords() { return submitMusic(domain::HotKeywordsQuery{}); }
quint64 BackendFacade::hotKeywordDetails() { return submitMusic(domain::HotKeywordDetailsQuery{}); }
quint64 BackendFacade::artistSongs(domain::ArtistSongsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::artistAlbums(domain::ArtistAlbumsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::playlistDetails(domain::PlaylistId playlist)
{
    return submitMusic(domain::PlaylistQuery{std::move(playlist)});
}
quint64 BackendFacade::playlistSongs(domain::PlaylistSongsQuery query)
{
    return submitMusic(std::move(query));
}

quint64 BackendFacade::discover(domain::DiscoveryBrowseQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::userProfile(domain::UserProfileQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::songWikiText(domain::SongWikiTextQuery query)
{
    if (query.sourceContext() != m_snapshot.token)
        return 0;
    return submitMusic(std::move(query));
}

quint64 BackendFacade::charts(domain::ChartListKind kind)
{
    return submitMusic(domain::ChartsQuery{kind});
}

quint64 BackendFacade::dailyRecommendedPlaylists()
{
    return submitMusic(domain::DailyRecommendedPlaylistsQuery{m_snapshot.accountId});
}
quint64 BackendFacade::recommendedPlaylists(int limit)
{
    return submitMusic(domain::RecommendedPlaylistsQuery{limit});
}
quint64 BackendFacade::playlistList(domain::PlaylistListQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::premiumPlaylists(domain::PremiumPlaylistsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::playlistTags(domain::PlaylistTagsKind kind)
{
    return submitMusic(domain::PlaylistTagsQuery{kind});
}

quint64 BackendFacade::searchAlbums(domain::SearchPage query)
{
    return submitMusic(domain::EntitySearchQuery{domain::MusicSearchKind::Album, std::move(query)});
}
quint64 BackendFacade::searchArtists(domain::SearchPage query)
{
    return submitMusic(
        domain::EntitySearchQuery{domain::MusicSearchKind::Artist, std::move(query)});
}
quint64 BackendFacade::searchPlaylists(domain::SearchPage query)
{
    return submitMusic(
        domain::EntitySearchQuery{domain::MusicSearchKind::Playlist, std::move(query)});
}
quint64 BackendFacade::searchPodcasts(domain::SearchPage query)
{
    return submitMusic(domain::PodcastSearchQuery{std::move(query)});
}
quint64 BackendFacade::searchVoices(domain::SearchPage query)
{
    return submitMusic(domain::VoiceSearchQuery{std::move(query)});
}

quint64 BackendFacade::podcastDetails(domain::PodcastId podcast)
{
    return submitMusic(domain::PodcastDetailQuery{std::move(podcast)});
}
quint64 BackendFacade::podcastPrograms(domain::PodcastProgramsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::programDetails(domain::ProgramId program)
{
    return submitMusic(domain::ProgramDetailQuery{std::move(program)});
}

quint64 BackendFacade::comments(domain::CommentsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::hotComments(domain::HotCommentsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::sortedComments(domain::SortedCommentsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::commentFloor(domain::CommentFloorQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::commentInfo(domain::CommentInfoQuery query)
{
    return submitMusic(std::move(query));
}

quint64 BackendFacade::dailyRecommendations()
{
    return submitMusic(domain::DailyRecommendationsQuery{});
}
quint64 BackendFacade::dailyRecommendationHistoryDates()
{
    return submitMusic(domain::DailyHistoryDatesQuery{m_snapshot.accountId});
}
quint64 BackendFacade::dailyRecommendationHistorySongs(domain::DailyHistorySongsQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (query.sourceContext() != m_snapshot.token || query.accountId() != m_snapshot.accountId)
        return 0;
    return submitMusic(std::move(query));
}
quint64 BackendFacade::personalFmBatch() { return submitMusic(domain::PersonalFmQuery{}); }

quint64 BackendFacade::myPlaylists(domain::CatalogPage page)
{
    return submitMusic(domain::MyPlaylistsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::likedSongIds()
{
    return submitMusic(domain::LikedSongIdsQuery{m_snapshot.accountId});
}
quint64 BackendFacade::savedAlbums(domain::CatalogPage page)
{
    return submitMusic(domain::SavedAlbumsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::savedArtists(domain::CatalogPage page)
{
    return submitMusic(domain::SavedArtistsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::subscribedPodcasts(domain::CatalogPage page)
{
    return submitMusic(domain::SubscribedPodcastsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::cloudSongs(domain::CatalogPage page)
{
    return submitMusic(domain::CloudSongsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::cloudSongDetails(QList<domain::CloudSongId> songs)
{
    return submitMusic(domain::CloudSongDetailsQuery{m_snapshot.accountId, std::move(songs)});
}
quint64 BackendFacade::recentSongs(int limit)
{
    return submitMusic(domain::RecentSongsQuery{m_snapshot.accountId, limit});
}
quint64 BackendFacade::recentPlaylists(int limit)
{
    return submitMusic(domain::RecentEntitiesQuery{m_snapshot.accountId,
                                                   domain::RecentEntityKind::Playlist, limit});
}
quint64 BackendFacade::recentAlbums(int limit)
{
    return submitMusic(
        domain::RecentEntitiesQuery{m_snapshot.accountId, domain::RecentEntityKind::Album, limit});
}
quint64 BackendFacade::recentPodcasts(int limit)
{
    return submitMusic(domain::RecentEntitiesQuery{m_snapshot.accountId,
                                                   domain::RecentEntityKind::Podcast, limit});
}
quint64 BackendFacade::recentVoices(int limit)
{
    return submitMusic(
        domain::RecentEntitiesQuery{m_snapshot.accountId, domain::RecentEntityKind::Voice, limit});
}
quint64 BackendFacade::listeningRanking(domain::RankingPeriod period)
{
    return submitMusic(domain::ListeningRankingQuery{m_snapshot.accountId, period});
}
quint64 BackendFacade::submitMusic(MusicQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() || m_music.size() >= 16 ||
        !validMusicQuery(query) ||
        (requiresAuthenticatedMusicQuery(query) &&
         m_snapshot.phase != domain::SessionPhase::Authenticated) ||
        (m_snapshot.phase != domain::SessionPhase::Guest &&
         m_snapshot.phase != domain::SessionPhase::Authenticated))
        return 0;
    const auto id = ++m_nextId;
    const MusicCommand command{id, m_generation, m_snapshot.token, std::move(query)};
    m_music.insert(id, command);
    emit musicRequested(command);
    return id;
}

void BackendFacade::cancelMusicWith(quint64 id, MusicError error)
{
    if (!m_music.contains(id))
        return;
    const auto command = m_music.take(id);
    emit musicCancelRequested(id);
    QTimer::singleShot(0, this,
                       [this, command, error] { publishMusic(command, MusicFailure{error}); });
}

void BackendFacade::invalidateMusic(MusicError error)
{
    for (auto id : m_music.keys())
        cancelMusicWith(id, error);
}

void BackendFacade::cancelMusic(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelMusicWith(id, MusicError::Canceled);
}

void BackendFacade::deliverMusic(MusicDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_music.contains(delivery.id))
        return;
    const auto expected = m_music.take(delivery.id);
    if (delivery.generation != expected.generation || delivery.context != expected.context ||
        expected.generation != m_generation || expected.context != m_snapshot.token ||
        m_state != BackendState::Ready ||
        (requiresAuthenticatedMusicQuery(expected.query) &&
         m_snapshot.phase != domain::SessionPhase::Authenticated) ||
        (musicQueryAccount(expected.query) &&
         *musicQueryAccount(expected.query) != m_snapshot.accountId))
        delivery.result = MusicFailure{MusicError::StaleContext};
    else if (const auto* browse = std::get_if<domain::DiscoveryBrowseQuery>(&expected.query);
             browse && !std::holds_alternative<MusicFailure>(delivery.result)) {
        const auto* page = std::get_if<domain::DiscoveryBrowsePage>(&delivery.result);
        if (!page || !domain::matchingDiscoveryBrowsePage(*browse, *page))
            delivery.result = MusicFailure{MusicError::InvalidResponse};
    }
    if (const auto* user = std::get_if<domain::UserProfileQuery>(&expected.query);
        user && !std::holds_alternative<MusicFailure>(delivery.result)) {
        const auto* page = std::get_if<domain::UserProfilePage>(&delivery.result);
        if (!page || !domain::matchingUserProfilePage(*user, *page))
            delivery.result = MusicFailure{MusicError::InvalidResponse};
    }
    if (const auto* wiki = std::get_if<domain::SongWikiTextQuery>(&expected.query);
        wiki && !std::holds_alternative<MusicFailure>(delivery.result)) {
        const auto* page = std::get_if<domain::SongWikiText>(&delivery.result);
        if (!page || !domain::matchingSongWikiText(*wiki, *page))
            delivery.result = MusicFailure{MusicError::InvalidResponse};
    }
    if (const auto* lyrics = std::get_if<PlaybackLyricQuery>(&expected.query)) {
        const auto source = playbackSource(lyrics->source.token.generation);
        if (!source || source->token != lyrics->source.token || source->song != lyrics->source.song ||
            source->provider != lyrics->source.provider || source->bili != lyrics->source.bili)
            delivery.result = MusicFailure{MusicError::StaleContext};
    }
    publishMusic(expected, std::move(delivery.result), delivery.cacheRetention);
}

void BackendFacade::publishMusic(const MusicCommand& command, MusicResult result,
                                 domain::CacheRetention retention)
{
    const bool resource = std::holds_alternative<DetailQuery>(command.query) ||
                          std::holds_alternative<LyricQuery>(command.query);
    if (!resource) {
        emit musicCompleted(command.id, command.context, std::move(result));
        return;
    }
    const auto source = m_snapshot;
    const QPointer<BackendFacade> guard(this);
    emit musicCompleted(command.id, command.context, result); // preserve legacy notification
    if (!guard)
        return;
    if (m_snapshot != source || m_state != BackendState::Ready ||
        m_generation != command.generation || m_snapshot.token != command.context)
        result = MusicFailure{MusicError::StaleContext};
    if (std::holds_alternative<MusicFailure>(result))
        retention = {};
    emit musicResourceCompleted(command.id, command.context, std::move(result), retention);
}

quint64 BackendFacade::reportListening(domain::ListeningReport report)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() ||
        m_snapshot.phase != domain::SessionPhase::Authenticated ||
        !domain::validMusicId(m_snapshot.accountId) || !report.isValid() || m_reports.size() >= 8)
        return 0;
    for (const auto& pending : std::as_const(m_reports))
        if (pending.report.eventId == report.eventId && pending.report.action == report.action)
            return 0;
    const auto id = ++m_nextId;
    const ListeningReportCommand command{id, m_generation, m_snapshot.token, m_snapshot.accountId,
                                         report};
    m_reports.insert(id, command);
    emit listeningReportRequested(command);
    return id;
}
void BackendFacade::cancelListeningReportWith(quint64 id, domain::ReportError error)
{
    if (!m_reports.contains(id))
        return;
    const auto command = m_reports.take(id);
    emit listeningReportCancelRequested(id);
    // The worker may already have sent it; do not claim cancellation undid a write.
    const domain::ListeningReportResult result{command.report.eventId, command.report.action,
                                               domain::ReportOutcome::Unknown, error};
    QTimer::singleShot(0, this, [this, id, context = command.context, result] {
        emit listeningReportCompleted(id, context, result);
    });
}
void BackendFacade::cancelListeningReport(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelListeningReportWith(id, domain::ReportError::Canceled);
}
void BackendFacade::invalidateListeningReports(domain::ReportError error)
{
    for (auto id : m_reports.keys())
        cancelListeningReportWith(id, error);
}
void BackendFacade::deliverListeningReport(ListeningReportDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_reports.contains(delivery.id))
        return;
    const auto expected = m_reports.take(delivery.id);
    if (delivery.generation != expected.generation || delivery.context != expected.context ||
        expected.generation != m_generation || expected.context != m_snapshot.token ||
        expected.accountId != m_snapshot.accountId || m_state != BackendState::Ready) {
        delivery.result = {expected.report.eventId, expected.report.action,
                           domain::ReportOutcome::Unknown, domain::ReportError::StaleContext};
    }
    delivery.result.eventId = expected.report.eventId;
    delivery.result.action = expected.report.action;
    emit listeningReportCompleted(expected.id, expected.context, delivery.result);
}

quint64 BackendFacade::mutateCollection(domain::CollectionMutation mutation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() ||
        m_snapshot.phase != domain::SessionPhase::Authenticated ||
        !domain::validMusicId(m_snapshot.accountId) || !mutation.isValid() ||
        m_collections.size() >= 8)
        return 0;
    const auto sourceContext = domain::collectionSourceContext(mutation);
    const auto sourceAccount = domain::collectionSourceAccount(mutation);
    if ((sourceContext && *sourceContext != m_snapshot.token) ||
        (sourceAccount && *sourceAccount != m_snapshot.accountId))
        return 0;
    for (const auto& pending : std::as_const(m_collections))
        if (pending.mutation.eventId == mutation.eventId)
            return 0;
    const auto id = ++m_nextId;
    const CollectionCommand command{id, m_generation, m_snapshot.token, m_snapshot.accountId,
                                    std::move(mutation)};
    m_collections.insert(id, command);
    emit collectionRequested(command);
    return id;
}
void BackendFacade::cancelCollectionWith(quint64 id, domain::CollectionError error)
{
    if (!m_collections.contains(id))
        return;
    const auto command = m_collections.take(id);
    emit collectionCancelRequested(id);
    domain::CollectionResult result{command.mutation.eventId, domain::CollectionOutcome::Unknown,
                                    error};
    if (std::holds_alternative<domain::PlaylistCoverReplace>(command.mutation.change))
        result.coverStep = domain::CoverStep::Unobserved;
    QTimer::singleShot(0, this, [this, id, context = command.context, result] {
        emit collectionCompleted(id, context, result);
    });
}
void BackendFacade::cancelCollection(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelCollectionWith(id, domain::CollectionError::Canceled);
}
void BackendFacade::invalidateCollections(domain::CollectionError error)
{
    for (const auto id : m_collections.keys())
        cancelCollectionWith(id, error);
}
void BackendFacade::deliverCollection(CollectionDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_collections.contains(delivery.id))
        return;
    const auto expected = m_collections.take(delivery.id);
    if (delivery.generation != expected.generation || delivery.context != expected.context ||
        expected.generation != m_generation || expected.context != m_snapshot.token ||
        expected.accountId != m_snapshot.accountId || m_state != BackendState::Ready ||
        m_snapshot.phase != domain::SessionPhase::Authenticated)
        delivery.result = {expected.mutation.eventId, domain::CollectionOutcome::Unknown,
                           domain::CollectionError::StaleContext};
    const bool creation = std::holds_alternative<domain::PlaylistCreate>(expected.mutation.change);
    if (creation && delivery.result.outcome == domain::CollectionOutcome::Acknowledged &&
        (!delivery.result.createdPlaylist ||
         !domain::validMusicId(delivery.result.createdPlaylist->value))) {
        delivery.result.outcome = domain::CollectionOutcome::Unknown;
        delivery.result.error = domain::CollectionError::InvalidResponse;
    }
    if (delivery.result.outcome != domain::CollectionOutcome::Acknowledged || !creation)
        delivery.result.createdPlaylist.reset();
    const bool cover =
        std::holds_alternative<domain::PlaylistCoverReplace>(expected.mutation.change);
    if (cover) {
        if (!delivery.result.coverStep ||
            *delivery.result.coverStep < domain::CoverStep::Unobserved ||
            *delivery.result.coverStep > domain::CoverStep::Committing)
            delivery.result.coverStep = domain::CoverStep::Unobserved;
        if (delivery.result.outcome == domain::CollectionOutcome::Acknowledged &&
            delivery.result.coverStep != domain::CoverStep::Committing) {
            delivery.result.outcome = domain::CollectionOutcome::Unknown;
            delivery.result.error = domain::CollectionError::InvalidResponse;
        }
    } else
        delivery.result.coverStep.reset();
    delivery.result.eventId = expected.mutation.eventId;
    emit collectionCompleted(expected.id, expected.context, delivery.result);
}

quint64 BackendFacade::mutateComment(domain::CommentMutation mutation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto command = m_commentWrites->submit(
        {m_state == BackendState::Ready, m_generation, m_snapshot}, std::move(mutation), m_nextId);
    if (!command)
        return 0;
    emit commentWriteRequested(*command);
    return command->id;
}
void BackendFacade::cancelCommentWith(quint64 id, domain::CommentWriteError error)
{
    const auto completion = m_commentWrites->cancel(id, error);
    if (!completion)
        return;
    emit commentWriteCancelRequested(id);
    QTimer::singleShot(0, this, [this, completion = *completion] {
        emit commentWriteCompleted(completion.id, completion.context, completion.result);
    });
}
void BackendFacade::cancelComment(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelCommentWith(id, domain::CommentWriteError::Canceled);
}
void BackendFacade::invalidateCommentWrites(domain::CommentWriteError error)
{
    for (const auto id : m_commentWrites->pendingIds())
        cancelCommentWith(id, error);
}
void BackendFacade::deliverCommentWrite(CommentWriteDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto completion = m_commentWrites->complete(
        {m_state == BackendState::Ready, m_generation, m_snapshot}, std::move(delivery));
    if (completion)
        emit commentWriteCompleted(completion->id, completion->context, completion->result);
}

quint64 BackendFacade::requestTogether(domain::TogetherRequest request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() ||
        m_snapshot.phase != domain::SessionPhase::Authenticated ||
        !domain::validMusicId(m_snapshot.accountId) || !request.isValid() || m_together.size() >= 8)
        return 0;
    for (const auto& p : std::as_const(m_together))
        if (p.request.eventId == request.eventId)
            return 0;
    const auto id = ++m_nextId;
    const TogetherCommand c{id, m_generation, m_snapshot.token, m_snapshot.accountId,
                            std::move(request)};
    m_together.insert(id, c);
    emit togetherRequested(c);
    return id;
}
void BackendFacade::cancelTogetherWith(quint64 id, domain::TogetherError error)
{
    if (!m_together.contains(id))
        return;
    const auto c = m_together.take(id);
    emit togetherCancelRequested(id);
    domain::TogetherResult r{c.request.eventId, domain::TogetherOutcome::Unknown, error,
                             domain::togetherKind(c.request.action), c.request.roomGeneration};
    QTimer::singleShot(
        0, this, [this, id, context = c.context, r] { emit togetherCompleted(id, context, r); });
}
void BackendFacade::cancelTogether(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelTogetherWith(id, domain::TogetherError::Canceled);
}
void BackendFacade::invalidateTogether(domain::TogetherError error)
{
    for (auto id : m_together.keys())
        cancelTogetherWith(id, error);
}
void BackendFacade::deliverTogether(TogetherDelivery d)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_together.contains(d.id))
        return;
    const auto c = m_together.take(d.id);
    domain::TogetherError error{};
    if (d.generation != c.generation || d.context != c.context || c.generation != m_generation ||
        c.context != m_snapshot.token || c.accountId != m_snapshot.accountId ||
        m_state != BackendState::Ready || m_snapshot.phase != domain::SessionPhase::Authenticated)
        error = domain::TogetherError::StaleContext;
    else if (d.result.eventId != c.request.eventId ||
             d.result.kind != domain::togetherKind(c.request.action) ||
             d.result.roomGeneration != c.request.roomGeneration ||
             d.result.outcome < domain::TogetherOutcome::NotSent ||
             d.result.outcome > domain::TogetherOutcome::Unknown)
        error = domain::TogetherError::InvalidResponse;
    if (error != domain::TogetherError::None)
        d.result = {c.request.eventId, domain::TogetherOutcome::Unknown, error};
    d.result.eventId = c.request.eventId;
    d.result.kind = domain::togetherKind(c.request.action);
    d.result.roomGeneration = c.request.roomGeneration;
    emit togetherCompleted(c.id, c.context, d.result);
}

void BackendFacade::invalidatePlayback()
{
    m_playbackGeneration = ++m_nextId;
    m_preloadGeneration = 0;
    m_preloadSong = {};
    m_playbackSources.clear();
    m_playback = {};
    // Deferred: account intent invalidation must not reenter submit before its command is emitted.
    const auto generation = m_playbackGeneration;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_playbackGeneration)
            emit playbackChanged(m_playback);
    });
}
quint64 BackendFacade::playSong(domain::SongId song, domain::AudioQuality quality,
                                domain::TransportIntent intent)
{
    PlaybackCommand command;
    command.action = PlaybackAction::Play;
    command.query = {std::move(song), quality, m_allowBiliSource};
    command.intent = intent;
    return submitPlayback(command);
}
quint64 BackendFacade::preloadSong(domain::SongId song, domain::AudioQuality quality)
{
    PlaybackCommand command;
    command.action = PlaybackAction::Preload;
    command.query = {std::move(song), quality, m_allowBiliSource};
    return submitPlayback(command);
}
quint64 BackendFacade::pausePlayback()
{
    PlaybackCommand c;
    c.action = PlaybackAction::Pause;
    return submitPlayback(c);
}
quint64 BackendFacade::resumePlayback()
{
    PlaybackCommand c;
    c.action = PlaybackAction::Resume;
    return submitPlayback(c);
}
quint64 BackendFacade::stopPlayback()
{
    PlaybackCommand c;
    c.action = PlaybackAction::Stop;
    return submitPlayback(c);
}
quint64 BackendFacade::seekPlayback(qint64 positionMs)
{
    PlaybackCommand c;
    c.action = PlaybackAction::Seek;
    c.positionMs = positionMs;
    return submitPlayback(c);
}
quint64 BackendFacade::setPlaybackVolume(float linear)
{
    PlaybackCommand c;
    c.action = PlaybackAction::Volume;
    c.volume = linear;
    return submitPlayback(c);
}
quint64 BackendFacade::setPlaybackNormalization(bool on)
{
    PlaybackCommand c;
    c.action = PlaybackAction::Normalize;
    c.normalize = on;
    return submitPlayback(c);
}
quint64 BackendFacade::setPlaybackTransition(domain::TransitionMode mode, qint64 crossfadeMs)
{
    PlaybackCommand c;
    c.action = PlaybackAction::Transition;
    c.transition = mode;
    c.crossfadeMs = crossfadeMs;
    return submitPlayback(c);
}
quint64 BackendFacade::setPlaybackVocals(quint64 generation, QList<domain::VocalSpan> spans)
{
    PlaybackCommand c;
    c.action = PlaybackAction::Vocals;
    c.next = {m_snapshot.token, generation};
    c.vocals = std::move(spans);
    return submitPlayback(c);
}
quint64 BackendFacade::submitPlayback(PlaybackCommand command)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || m_pending.size() >= 32 || !m_snapshot.token.isValid() ||
        (m_snapshot.phase != domain::SessionPhase::Guest &&
         m_snapshot.phase != domain::SessionPhase::Authenticated))
        return 0;
    if (command.action == PlaybackAction::Play &&
        (!domain::validMusicId(command.query.song.value) ||
         domain::qualityName(command.query.quality).isEmpty() ||
         (command.intent != domain::TransportIntent::Play &&
          command.intent != domain::TransportIntent::Pause)))
        return 0;
    if (command.action == PlaybackAction::Preload &&
        ((!command.query.song.value.isEmpty() && !domain::validMusicId(command.query.song.value)) ||
         domain::qualityName(command.query.quality).isEmpty() || !m_playbackGeneration))
        return 0;
    if (command.action == PlaybackAction::Seek &&
        (command.positionMs < 0 || command.positionMs > 7LL * 24 * 60 * 60 * 1000))
        return 0;
    if (command.action == PlaybackAction::Volume &&
        (!std::isfinite(command.volume) || command.volume < 0 || command.volume > 1))
        return 0;
    if (command.action == PlaybackAction::Vocals &&
        (!command.next.generation ||
         (command.next.generation != m_playbackGeneration &&
          command.next.generation != m_preloadGeneration) ||
         command.vocals.size() > 2000 ||
         std::any_of(command.vocals.begin(), command.vocals.end(), [](const domain::VocalSpan& s) {
             return s.startMs < -60000 || s.endMs <= s.startMs ||
                    s.endMs > 7LL * 24 * 60 * 60 * 1000;
         })))
        return 0;
    if (command.action == PlaybackAction::Transition &&
        (command.transition < domain::TransitionMode::Smart ||
         command.transition > domain::TransitionMode::Gapless || command.crossfadeMs < 1000 ||
         command.crossfadeMs > 12000))
        return 0;
    command.id = ++m_nextId;
    command.generation = m_generation;
    m_pending.insert(command.id);
    const bool replacing =
        command.action == PlaybackAction::Play || command.action == PlaybackAction::Stop;
    if (replacing)
        m_playbackSources.clear();
    else if (command.action == PlaybackAction::Preload)
        m_playbackSources.remove(m_preloadGeneration);
    if (replacing || command.action == PlaybackAction::Preload) {
        m_preloadGeneration = 0;
        m_preloadSong = {};
    }
    if (command.action == PlaybackAction::Preload && !command.query.song.value.isEmpty()) {
        m_preloadGeneration = command.id;
        m_preloadSong = command.query.song;
        command.next = {m_snapshot.token, command.id};
    }
    if (replacing) {
        m_playbackGeneration = command.id;
        m_playback = {};
        m_playback.token = {m_snapshot.token, m_playbackGeneration};
        if (command.action == PlaybackAction::Play) {
            m_playback.song = command.query.song;
            m_playback.state = domain::PlaybackState::Resolving;
            m_playback.intent = command.intent;
        }
    }
    command.token = {m_snapshot.token, m_playbackGeneration};
    emit playbackRequested(command);
    if (replacing && m_playback.token == command.token)
        emit playbackChanged(m_playback);
    return command.id;
}
void BackendFacade::deliverPlayback(PlaybackDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || delivery.generation != m_generation ||
        delivery.snapshot.token.session != m_snapshot.token)
        return;
    const auto generation = delivery.snapshot.token.generation;
    if (generation && generation == m_preloadGeneration) {
        m_playbackSources.remove(m_playbackGeneration);
        m_playbackGeneration = std::exchange(m_preloadGeneration, 0); // played on into it
        m_preloadSong = {};
    } else if (generation != m_playbackGeneration)
        return;
    m_playback = std::move(delivery.snapshot);
    emit playbackChanged(m_playback);
}

void BackendFacade::finishClose(bool persistenceFailed)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == BackendState::Stopped)
        return;
    m_snapshot = {};
    setState(BackendState::Stopped);
    emit stopped(persistenceFailed);
}

} // namespace cirrus::application
