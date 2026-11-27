#pragma once
#include "logging/log.h"

#include "application/account/qr_login_service.h"
#include "application/backend/comment_write_messages.h"
#include "application/ports/music_gateway.h"
#include "domain/music/account_library_types.h"
#include "domain/music/collection_types.h"
#include "domain/music/comment_write_types.h"
#include "domain/music/entity_search_types.h"
#include "domain/music/podcast_types.h"
#include "domain/music/together_types.h"
#include "domain/playback/listening_report.h"
#include "domain/playback/playback_types.h"
#include <QHash>

#include <QSet>
#include <memory>

namespace cirrus::application
{

namespace detail
{
class CommentWriteRequests;
}

enum class BackendState { Dormant, Starting, Ready, Failed, Stopping, Stopped };
enum class StartupError { None, Configuration, Storage, GuestBootstrap };
CIRRUS_LOG_ENUM(StartupError, None, Configuration, Storage, GuestBootstrap)
enum class AccountAction {
    BeginLogin,
    Restore,
    Poll,
    Cancel,
    SignOut,
    BeginCredentialLogin,
    SendSmsCode
};
enum class PersistenceArea { ProtocolKey, AnonymousToken };
CIRRUS_LOG_ENUM(PersistenceArea, ProtocolKey, AnonymousToken)

struct BackendCommand {
    quint64 id = 0;
    quint64 generation = 0;
    AccountAction action{};
    QrLoginOptions options;
    domain::CredentialLogin credential; // BeginCredentialLogin only; ephemeral
    domain::SmsCodeRequest sms;         // SendSmsCode only
};

struct MusicCommand {
    quint64 id = 0;
    quint64 generation = 0;
    domain::SessionToken context;
    MusicQuery query;
};
struct MusicDelivery {
    quint64 id = 0;
    quint64 generation = 0;
    domain::SessionToken context;
    MusicResult result;
    domain::CacheRetention cacheRetention{};
};

struct ListeningReportCommand {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    QString accountId;
    domain::ListeningReport report;
};
struct ListeningReportDelivery {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    domain::ListeningReportResult result;
};

struct TogetherCommand {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    QString accountId;
    domain::TogetherRequest request;
};
struct TogetherDelivery {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    domain::TogetherResult result;
};

struct CollectionCommand {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    QString accountId;
    domain::CollectionMutation mutation;
};
struct CollectionDelivery {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    domain::CollectionResult result;
};

enum class PlaybackAction {
    Play,
    Pause,
    Resume,
    Stop,
    Seek,
    Volume,
    Preload,
    Normalize,
    Transition,
    Vocals
};
struct PlaybackCommand {
    quint64 id = 0;
    quint64 generation = 0; // account intent barrier
    domain::LoadToken token;
    PlaybackAction action{};
    domain::StreamQuery query;
    qint64 positionMs = 0;
    float volume = 0.5f;
    bool normalize = false; // Normalize: volume normalization on or off
    // Transition: how one track goes into the next, and the Crossfade mode's length.
    domain::TransitionMode transition = domain::TransitionMode::Smart;
    qint64 crossfadeMs = 6000;
    QList<domain::VocalSpan> vocals; // Vocals: the sung lines of the load `next` names
    domain::TransportIntent intent = domain::TransportIntent::Play;
    domain::LoadToken next; // Preload: the generation it plays under; empty song cancels
};
struct PlaybackDelivery {
    quint64 generation = 0;
    domain::PlaybackSnapshot snapshot; // NEVER contains a URL or provider payload
};

// A value-only, internal queued delivery envelope. No infrastructure objects or
// credentials cross threads. Generation invalidates events already queued to UI.
struct BackendEvent {
    enum class Kind {
        Started,
        Session,
        Challenge,
        Status,
        SignedIn,
        AuthFailed,
        Saved,
        Acknowledged,
        StorageWarning,
        SmsCode
    };
    Kind kind{};
    quint64 generation = 0;
    quint64 commandId = 0;
    bool accepted = false;
    StartupError startupError{};
    PersistenceArea persistenceArea{};
    domain::SessionSnapshot session;
    domain::QrLoginChallenge challenge;
    domain::QrLoginStatus status{};
    domain::AccountIdentity identity;
    AuthFailure failure;
    SavedSessionResult saved = false;
    SmsCodeResult smsCode = false;
};

// Caller/UI-thread asynchronous surface. No QML dependency and no synchronous
// cross-thread reads. Runtime installs queued signal/slot wiring before use.
// One-shot start/close. Command ID 0 means rejected locally (not ready/overload).
// Other IDs receive an acknowledgement, NOT proof that authentication succeeded.
// close() cancels unobserved acknowledgements with false; that is NOT evidence
// that a previously queued command had no side effects. Observe storage results.
class BackendFacade final : public QObject
{
    Q_OBJECT
  public:
    explicit BackendFacade(QObject* parent = nullptr);
    ~BackendFacade() override;
    [[nodiscard]] BackendState state() const { return m_state; }
    [[nodiscard]] domain::SessionSnapshot snapshot() const { return m_snapshot; }
    void start();
    void close(); // does NOT sign out; keep event loop alive until stopped()
    [[nodiscard]] quint64 beginLogin(QrLoginOptions options = {});
    // Phone/email sign-in (docs/api/credential-login.md); results like beginLogin
    // (signedIn/failed), without a challenge. Password methods take passwordDigest().
    [[nodiscard]] quint64 beginCredentialLogin(domain::CredentialLogin login,
                                               QrLoginOptions options = {});
    // An SMS code for a PhoneCode login; no session change. Result: smsCodeSent.
    [[nodiscard]] quint64 sendSmsCode(domain::SmsCodeRequest request);
    [[nodiscard]] quint64 restore();
    [[nodiscard]] quint64 poll();
    [[nodiscard]] quint64 cancel();
    [[nodiscard]] quint64 signOut();
    [[nodiscard]] quint64 searchSongs(domain::SongSearch query);
    [[nodiscard]] quint64 searchAlbums(domain::SearchPage query);
    [[nodiscard]] quint64 searchArtists(domain::SearchPage query);
    [[nodiscard]] quint64 searchPlaylists(domain::SearchPage query);
    [[nodiscard]] quint64 searchPodcasts(domain::SearchPage query);
    [[nodiscard]] quint64 searchVoices(domain::SearchPage query);
    [[nodiscard]] quint64 podcastDetails(domain::PodcastId podcast);
    [[nodiscard]] quint64 podcastPrograms(domain::PodcastProgramsQuery query);
    [[nodiscard]] quint64 programDetails(domain::ProgramId program);
    [[nodiscard]] quint64 comments(domain::CommentsQuery query);
    [[nodiscard]] quint64 hotComments(domain::HotCommentsQuery query);
    [[nodiscard]] quint64 sortedComments(domain::SortedCommentsQuery query);
    [[nodiscard]] quint64 commentFloor(domain::CommentFloorQuery query);
    [[nodiscard]] quint64 commentInfo(domain::CommentInfoQuery query);
    [[nodiscard]] quint64 songDetails(QList<domain::SongId> songs);
    [[nodiscard]] quint64 lyrics(domain::SongId song);
    [[nodiscard]] quint64 playbackLyrics(quint64 generation);
    [[nodiscard]] quint64 playbackLyricGeneration(const domain::SongId& song) const;
    [[nodiscard]] std::optional<domain::PlaybackSourceSelection>
    playbackSource(quint64 generation) const;
    void deliverPlaybackSource(domain::PlaybackSourceSelection selection);
    [[nodiscard]] quint64 albumDetails(domain::AlbumId album);
    [[nodiscard]] quint64 artistDetails(domain::ArtistId artist);
    [[nodiscard]] quint64 artistIntroduction(domain::ArtistId artist);
    [[nodiscard]] quint64 songWikiSummary(domain::SongId song);
    [[nodiscard]] quint64 searchSuggestions(QString keywords);
    [[nodiscard]] quint64 hotKeywords();
    [[nodiscard]] quint64 hotKeywordDetails();
    [[nodiscard]] quint64 artistSongs(domain::ArtistSongsQuery query);
    [[nodiscard]] quint64 artistAlbums(domain::ArtistAlbumsQuery query);
    [[nodiscard]] quint64 playlistDetails(domain::PlaylistId playlist);
    [[nodiscard]] quint64 playlistSongs(domain::PlaylistSongsQuery query);
    [[nodiscard]] quint64 discover(domain::DiscoveryBrowseQuery query);
    [[nodiscard]] quint64 songWikiText(domain::SongWikiTextQuery query);
    // Any user's profile, lists and ranking (docs/api/user-profile-reads.md); guests too.
    [[nodiscard]] quint64 userProfile(domain::UserProfileQuery query);
    [[nodiscard]] quint64 dailyRecommendations();
    [[nodiscard]] quint64 dailyRecommendationHistoryDates();
    [[nodiscard]] quint64 dailyRecommendationHistorySongs(domain::DailyHistorySongsQuery query);
    [[nodiscard]] quint64 personalFmBatch(); // use one PersonalFmSource for dynamic consumption
    // 心动模式's list for the liked songs (docs/api/heartbeat-mode.md); signed in only.
    [[nodiscard]] quint64 heartbeatList(domain::HeartbeatQuery query);
    [[nodiscard]] quint64 myPlaylists(domain::CatalogPage page = {});
    [[nodiscard]] quint64 likedSongIds();
    [[nodiscard]] quint64 savedAlbums(domain::CatalogPage page = {25, 0});
    [[nodiscard]] quint64 savedArtists(domain::CatalogPage page = {25, 0});
    [[nodiscard]] quint64 subscribedPodcasts(domain::CatalogPage page = {30, 0});
    [[nodiscard]] quint64 cloudSongs(domain::CatalogPage page = {30, 0});
    [[nodiscard]] quint64 cloudSongDetails(QList<domain::CloudSongId> songs);
    [[nodiscard]] quint64 recentSongs(int limit = 100);
    [[nodiscard]] quint64 recentPlaylists(int limit = 100);
    [[nodiscard]] quint64 recentAlbums(int limit = 100);
    [[nodiscard]] quint64 recentPodcasts(int limit = 100);
    [[nodiscard]] quint64 recentVoices(int limit = 100);
    [[nodiscard]] quint64
    listeningRanking(domain::RankingPeriod period = domain::RankingPeriod::AllTime);
    [[nodiscard]] quint64 charts(domain::ChartListKind kind = domain::ChartListKind::Overview);
    [[nodiscard]] quint64 recommendedPlaylists(int limit = 30);
    [[nodiscard]] quint64 dailyRecommendedPlaylists();
    [[nodiscard]] quint64 playlistList(domain::PlaylistListQuery query = {});
    [[nodiscard]] quint64 premiumPlaylists(domain::PremiumPlaylistsQuery query = {});
    [[nodiscard]] quint64
    playlistTags(domain::PlaylistTagsKind kind = domain::PlaylistTagsKind::Catalogue);
    void cancelMusic(quint64 id);
    [[nodiscard]] quint64
    reportListening(domain::ListeningReport report); // explicit write, NEVER called by playback
    void cancelListeningReport(quint64 id);
    [[nodiscard]] quint64 mutateCollection(domain::CollectionMutation mutation);
    void cancelCollection(quint64 id);
    [[nodiscard]] quint64 mutateComment(domain::CommentMutation mutation);
    void cancelComment(quint64 id);
    [[nodiscard]] quint64 requestTogether(domain::TogetherRequest request);
    void cancelTogether(quint64 id);
    void setAllowBiliSource(bool enabled) { m_allowBiliSource = enabled; }
    [[nodiscard]] bool allowBiliSource() const { return m_allowBiliSource; }
    [[nodiscard]] domain::PlaybackSnapshot playback() const { return m_playback; }
    [[nodiscard]] quint64 playSong(domain::SongId song,
                                   domain::AudioQuality quality = domain::AudioQuality::Standard,
                                   domain::TransportIntent intent = domain::TransportIntent::Play,
                                   bool permitBiliSource = true);
    // The song to follow the current one without a gap; it plays under the returned
    // generation (playback().token changes to it when it starts). A new play, stop or
    // preload replaces it; an empty song cancels.
    [[nodiscard]] quint64
    preloadSong(domain::SongId song, domain::AudioQuality quality = domain::AudioQuality::Standard,
                bool permitBiliSource = true);
    [[nodiscard]] quint64 pausePlayback();
    [[nodiscard]] quint64 resumePlayback();
    [[nodiscard]] quint64 stopPlayback();
    [[nodiscard]] quint64 seekPlayback(qint64 positionMs);
    [[nodiscard]] quint64 setPlaybackVolume(float linear);
    // Volume normalization (docs/tasks/audio-engine.md §6.5), a setting: like the volume
    // it needs no load and outlives one.
    [[nodiscard]] quint64 setPlaybackNormalization(bool on);
    // How one track goes into the next (docs/tasks/automix.md §3), a setting the same way.
    [[nodiscard]] quint64 setPlaybackTransition(domain::TransitionMode mode, qint64 crossfadeMs);
    // The sung lines of the current load or the preloaded one (by its generation), for the
    // transitions (docs/tasks/automix.md §7): times only, never the song.
    [[nodiscard]] quint64 setPlaybackVocals(quint64 generation, QList<domain::VocalSpan> spans);

    // Runtime delivery endpoints; queued connections only, not public commands.
    void deliver(BackendEvent event);
    void finishClose(bool persistenceFailed);
    void deliverMusic(MusicDelivery delivery);
    void deliverPlayback(PlaybackDelivery delivery);
    void deliverListeningReport(ListeningReportDelivery delivery);
    void deliverCollection(CollectionDelivery delivery);
    void deliverCommentWrite(CommentWriteDelivery delivery);
    void deliverTogether(TogetherDelivery delivery);

  signals:
    void stateChanged(cirrus::application::BackendState state);
    void started(
        cirrus::application::StartupError error); // GuestBootstrap = degraded, QR still available
    void
    stopped(bool persistenceFailed); // all jobs settled; false is not live backend certification
    void closeDelayed();             // once after 5s; no force termination or false stopped result
    void sessionChanged(cirrus::domain::SessionSnapshot snapshot);
    void challengeReady(cirrus::domain::SessionToken context,
                        cirrus::domain::QrLoginChallenge challenge);
    void statusChanged(cirrus::domain::SessionToken context, cirrus::domain::QrLoginStatus status);
    void signedIn(cirrus::domain::SessionToken context, cirrus::domain::AccountIdentity identity);
    void failed(cirrus::domain::SessionToken context, cirrus::application::AuthFailure failure);
    void savedSessionCompleted(cirrus::domain::SessionToken context,
                               cirrus::application::SavedSessionResult result);
    void smsCodeSent(cirrus::domain::SessionToken context,
                     cirrus::application::SmsCodeResult result);
    void acknowledged(quint64 commandId, bool accepted);
    void persistenceWarning(cirrus::application::PersistenceArea area);
    void musicCompleted(quint64 id, cirrus::domain::SessionToken context,
                        cirrus::application::MusicResult result);
    // Only standalone details/lyrics; atomic original-result + retention delivery.
    void musicResourceCompleted(quint64 id, cirrus::domain::SessionToken context,
                                cirrus::application::MusicResult result,
                                cirrus::domain::CacheRetention retention);

    void listeningReportCompleted(quint64 id, cirrus::domain::SessionToken context,
                                  cirrus::domain::ListeningReportResult result);
    void collectionCompleted(quint64 id, cirrus::domain::SessionToken context,
                             cirrus::domain::CollectionResult result);
    void commentWriteCompleted(quint64 id, cirrus::domain::SessionToken context,
                               cirrus::domain::CommentWriteResult result);
    void togetherCompleted(quint64 id, cirrus::domain::SessionToken context,
                           cirrus::domain::TogetherResult result);
    void playbackChanged(cirrus::domain::PlaybackSnapshot snapshot);
    void playbackSourceSelected(cirrus::domain::PlaybackSourceSelection selection);

    // Composition wiring only; emitted on this object's thread.
    void startRequested();
    void closeRequested();
    void commandRequested(cirrus::application::BackendCommand command);
    void musicRequested(cirrus::application::MusicCommand command);
    void musicCancelRequested(quint64 id);
    void playbackRequested(cirrus::application::PlaybackCommand command);
    void listeningReportRequested(cirrus::application::ListeningReportCommand command);
    void listeningReportCancelRequested(quint64 id);
    void collectionRequested(cirrus::application::CollectionCommand command);
    void collectionCancelRequested(quint64 id);
    void commentWriteRequested(cirrus::application::CommentWriteCommand command);
    void commentWriteCancelRequested(quint64 id);
    void togetherRequested(cirrus::application::TogetherCommand command);
    void togetherCancelRequested(quint64 id);

  private:
    void invalidateListeningReports(domain::ReportError error);
    void cancelListeningReportWith(quint64 id, domain::ReportError error);
    QHash<quint64, ListeningReportCommand> m_reports;
    QHash<quint64, CollectionCommand> m_collections;
    std::unique_ptr<detail::CommentWriteRequests> m_commentWrites;
    QHash<quint64, TogetherCommand> m_together;
    void invalidateTogether(domain::TogetherError error);
    void cancelTogetherWith(quint64 id, domain::TogetherError error);
    void invalidateCommentWrites(domain::CommentWriteError error);
    void cancelCommentWith(quint64 id, domain::CommentWriteError error);
    void invalidateCollections(domain::CollectionError error);
    void cancelCollectionWith(quint64 id, domain::CollectionError error);
    quint64 submit(AccountAction action, QrLoginOptions options = {},
                   domain::CredentialLogin credential = {}, domain::SmsCodeRequest sms = {});
    void setState(BackendState state);
    quint64 submitMusic(MusicQuery query);
    void publishMusic(const MusicCommand& command, MusicResult result,
                      domain::CacheRetention retention = {});
    quint64 submitPlayback(PlaybackCommand command);
    void invalidatePlayback();
    QHash<quint64, domain::PlaybackSourceSelection> m_playbackSources;
    domain::SongId m_preloadSong;
    bool m_allowBiliSource = false;
    domain::PlaybackSnapshot m_playback;
    quint64 m_playbackGeneration = 0;
    quint64 m_preloadGeneration = 0; // the preloaded song's, adopted when it starts
    void invalidateMusic(MusicError error);
    void cancelMusicWith(quint64 id, MusicError error);
    BackendState m_state = BackendState::Dormant;
    domain::SessionSnapshot m_snapshot;
    QSet<quint64> m_pending;
    QHash<quint64, MusicCommand> m_music;
    quint64 m_nextId = 0;
    quint64 m_generation = 0;
};

} // namespace cirrus::application

Q_DECLARE_METATYPE(cirrus::application::BackendState)
Q_DECLARE_METATYPE(cirrus::application::StartupError)
Q_DECLARE_METATYPE(cirrus::application::PersistenceArea)
Q_DECLARE_METATYPE(cirrus::application::BackendCommand)
Q_DECLARE_METATYPE(cirrus::application::BackendEvent)
Q_DECLARE_METATYPE(cirrus::application::MusicCommand)
Q_DECLARE_METATYPE(cirrus::application::MusicDelivery)
Q_DECLARE_METATYPE(cirrus::application::PlaybackCommand)
Q_DECLARE_METATYPE(cirrus::application::PlaybackDelivery)
Q_DECLARE_METATYPE(cirrus::application::ListeningReportCommand)
Q_DECLARE_METATYPE(cirrus::application::ListeningReportDelivery)
Q_DECLARE_METATYPE(cirrus::application::CollectionCommand)
Q_DECLARE_METATYPE(cirrus::application::CollectionDelivery)
Q_DECLARE_METATYPE(cirrus::application::TogetherCommand)
Q_DECLARE_METATYPE(cirrus::application::TogetherDelivery)
