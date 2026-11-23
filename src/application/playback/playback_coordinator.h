#pragma once
#include "application/account/session_coordinator.h"
#include "application/ports/playback.h"
#include <QElapsedTimer>
#include <QTimer>

namespace cirrus::application
{
// One load at a time, plus the one preloaded to follow it without a gap; queue/FM/lyrics
// live in ListeningSession.
class PlaybackCoordinator final : public QObject
{
    Q_OBJECT
  public:
    PlaybackCoordinator(SessionCoordinator& session, IStreamResolver& resolver,
                        IPlaybackBackend& backend, QObject* parent = nullptr);
    ~PlaybackCoordinator() override;
    bool start(domain::LoadToken token, domain::StreamQuery query,
               domain::TransportIntent intent = domain::TransportIntent::Play);
    void stop(domain::LoadToken token = {});
    bool pause();
    bool resume();
    bool seek(qint64 positionMs);
    void setVolume(float linear);
    void setNormalization(bool on);
    void setTransition(domain::TransitionMode mode, qint64 crossfadeMs);
    void setVocals(domain::LoadToken token, const QList<domain::VocalSpan>& spans);
    // Resolves `query` now and hands it to the backend to follow `current` without a gap
    // under `next` (a fresh generation); no query cancels. When the backend gets there the
    // snapshot changes to `next`. Refused when `current` isn't the current load.
    bool preload(domain::LoadToken current, domain::LoadToken next,
                 std::optional<domain::StreamQuery> query);
    [[nodiscard]] domain::PlaybackSnapshot snapshot() const { return m_snapshot; }
  signals:
    void changed(cirrus::domain::PlaybackSnapshot snapshot);
    void sourceSelected(cirrus::domain::PlaybackSourceSelection selection);

  private:
    struct Next {
        domain::LoadToken token;
        domain::StreamQuery query;
        quint64 request = 0;             // resolving
        bool loaded = false;             // handed to the backend
        QList<domain::VocalSpan> vocals; // until it is
        QString actualQuality;
        std::optional<domain::PreviewRange> preview;
        domain::SourceProvider provider = domain::SourceProvider::Netease;
    };
    struct Refresh {
        domain::LoadToken token;
        domain::StreamQuery query;
        quint64 request = 0;
    };
    void resolved(quint64 id, domain::SessionToken context, StreamResult result);
    void preloaded(StreamResult result);
    void advance();
    void expired(domain::LoadToken token);
    void dropNext();
    void receive(PlaybackEvent event);
    void fail(domain::PlaybackFailure failure);
    void retire();
    void applyIntent();
    bool current(domain::LoadToken token) const;
    SessionCoordinator& m_session;
    IStreamResolver& m_resolver;
    IPlaybackBackend& m_backend;
    domain::PlaybackSnapshot m_snapshot;
    quint64 m_request = 0;
    quint64 m_lastGeneration = 0; // never reuse a load identity, even after stop/session change
    bool m_ready = false;
    QTimer m_deadline;
    QElapsedTimer m_loadClock; // since start(), for the log
    std::optional<qint64> m_seek;
    domain::StreamQuery m_query; // the current load's, for refreshing its address
    // The current load's sung lines, until it is handed to the backend (the lyrics often
    // come before the address).
    QList<domain::VocalSpan> m_vocals;
    std::optional<Next> m_next;
    QList<Refresh> m_refreshes;
};
} // namespace cirrus::application
