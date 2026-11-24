#pragma once

#include "application/backend/backend_facade.h"
#include "domain/account/bili_account_types.h"

#include <QHash>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace cirrus::netease
{
class HttpTransport;
class CoverUpload;
class CommentV3Issuer;
} // namespace cirrus::netease
namespace cirrus::application
{
class IPlaybackBackend;
class ListeningSession;
class PersonalFmSource;
} // namespace cirrus::application
namespace cirrus::secrets
{
class SecretStore;
}

namespace cirrus::storage
{
class ListeningStateService;
}

namespace cirrus::cache
{
class ResourceCacheService;
class CachedMusicQueryService;
} // namespace cirrus::cache

namespace cirrus::runtime
{

struct BackendConfig {
    QString databasePath;    // absolute installation state.sqlite path; not a credentials file
    QString keychainService; // caller-selected application identity; no invented app ID
    bool persistListeningState = false; // explicit local metadata persistence; no product opt-in
    QString resourceCacheRoot{};        // empty=disabled; explicit existing private Linux directory
    QString audioCacheRoot{}; // empty: audio streams through temporary files only (ADR-0008)
};

// Factories run on the backend thread, never return pre-created GUI-thread QObjects.
// Empty factories select native Qt Network and QtKeychain. Offline tests inject both.
struct BackendFactories {
    std::function<std::unique_ptr<secrets::SecretStore>()> secrets;
    std::function<std::unique_ptr<netease::HttpTransport>()> apiTransport;
    std::function<std::unique_ptr<netease::HttpTransport>()> keyTransport;
    std::function<std::unique_ptr<application::IPlaybackBackend>()> playback;
    std::function<std::unique_ptr<QNetworkAccessManager>()> biliStreamHttp;
    std::function<std::unique_ptr<QNetworkAccessManager>()> biliLyricsHttp;
    std::function<std::unique_ptr<QNetworkAccessManager>()> biliAccountHttp;
    std::function<std::unique_ptr<netease::CoverUpload>()> coverUpload; // same-thread NOS test seam
    std::function<std::unique_ptr<netease::CommentV3Issuer>()>
        commentV3Issuer; // same-thread, no real issuer in offline tests
};

// Composition root only; the template app does not instantiate it yet.
// Owns a caller-thread facade and a self-draining backend thread. Destruction
// requests asynchronous close, never blocks the caller. The process event loop
// must stay alive until facade.stopped(); destroying this handle is NOT erasure.
class BackendRuntime final : public QObject
{
    Q_OBJECT
  public:
    BackendRuntime(BackendConfig config, BackendFactories factories = {},
                   QObject* parent = nullptr);
    ~BackendRuntime() override;
    [[nodiscard]] application::BackendFacade& facade() const { return *m_facade; }
    [[nodiscard]] application::ListeningSession& listening() const { return *m_listening; }
    [[nodiscard]] application::PersonalFmSource& fm() const { return *m_fm; }
    [[nodiscard]] storage::ListeningStateService* listeningState() const { return m_state; }
    [[nodiscard]] cache::ResourceCacheService* resourceCache() const { return m_cache; }
    [[nodiscard]] cache::CachedMusicQueryService* resourceQueries() const
    {
        return m_resourceQueries;
    }
    // The song cache (media::AudioStore, on the backend thread): bytes on disk for every
    // account. Replies come later on this object's thread; -1 while the backend isn't
    // running. Clearing replies with the usage after it (files being played go when they
    // close).
    void audioCacheUsage(std::function<void(qint64 bytes)> reply);
    void clearAudioCache(std::function<void(qint64 bytes)> reply);
    // Applies now, and to the store the backend creates when it starts.
    void setAudioCacheCapacity(qint64 bytes);
    void saveBiliCookie(QByteArray cookie);
    void clearBiliCookie();
    [[nodiscard]] domain::BiliAccountState biliAccount() const { return m_biliAccount; }

  signals:
    // Internal: carries the calls above to the backend thread.
    void audioCacheRequested(quint64 id, int action, qint64 value);
    void biliCookieRequested(QByteArray cookie, bool clear);
    void biliAccountChanged(cirrus::domain::BiliAccountState state);

  private:
    void audioCacheDelivered(quint64 id, qint64 bytes);
    QHash<quint64, std::function<void(qint64)>> m_audioReplies;
    quint64 m_audioRequest = 0;
    domain::BiliAccountState m_biliAccount;
    application::BackendFacade* m_facade = nullptr;
    application::PersonalFmSource* m_fm = nullptr;
    application::ListeningSession* m_listening = nullptr;
    storage::ListeningStateService* m_state = nullptr;
    cache::ResourceCacheService* m_cache = nullptr;
    cache::CachedMusicQueryService* m_resourceQueries = nullptr;
};

} // namespace cirrus::runtime
