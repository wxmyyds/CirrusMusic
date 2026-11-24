#include "music_gateway.h"
#include "account_library_mapper.h"
#include "catalog_mapper.h"
#include "chart_mapper.h"
#include "cloud_history_mapper.h"
#include "comment_mapper.h"
#include "entity_search_mapper.h"
#include "infrastructure/netease/api/discovery_browse_operation.h"
#include "library_mapper.h"
#include "logging/log.h"
#include "music_mapper.h"
#include "playlist_discovery_mapper.h"
#include "podcast_mapper.h"
#include "recent_entity_mapper.h"
#include "recommendation_history_mapper.h"
#include "search_assist_mapper.h"
#include "user_profile_mapper.h"

#include <QJsonArray>
#include <QSet>
#include <QThread>
#include <QTimer>

namespace cirrus::netease
{
using namespace application;
namespace
{
DetailQuery pageDetails(const domain::PlaylistSongsPage& page)
{
    DetailQuery query;
    QSet<QString> seen;
    for (const auto& id : page.ids)
        if (!seen.contains(id.value)) {
            seen.insert(id.value);
            query.songs.append(id);
        }
    return query;
}
QJsonObject detailParameters(const DetailQuery& query)
{
    QJsonArray ids;
    for (const auto& song : query.songs)
        ids.append(song.value);
    return {{QStringLiteral("ids"), ids}};
}
MusicFailure failure(const ApiResult& result)
{
    MusicFailure error{MusicError::ProtocolFailure};
    if (result.error == ApiError::StaleContext)
        error.error = MusicError::StaleContext;
    else if (result.error == ApiError::Canceled ||
             result.transportError == TransportError::Canceled)
        error.error = MusicError::Canceled;
    else if (result.error == ApiError::Busy)
        error.error = MusicError::Busy;
    else if (result.error == ApiError::InvalidRequest)
        error.error = MusicError::InvalidRequest;
    else if (result.error == ApiError::Decode)
        error.error = MusicError::InvalidResponse;
    else if (result.transportError == TransportError::Timeout)
        error.error = MusicError::Timeout;
    else if (result.httpStatus == 429 || result.decoded.businessCode == 429) {
        error.error = MusicError::RateLimited;
        bool ok = false;
        const auto seconds = result.retryAfter.toLongLong(&ok);
        if (ok && seconds >= 0 && seconds <= 86400)
            error.retryAfterMs = seconds * 1000;
    } else if (result.error == ApiError::Transport)
        error.error = MusicError::Network;
    else if (result.decoded.businessCode)
        error.error = MusicError::ProviderRejected;
    else if (result.error == ApiError::None)
        error.error = MusicError::InvalidResponse;
    error.providerCode = result.decoded.businessCode;
    return error;
}
} // namespace

MusicGateway::MusicGateway(SessionApiClient& client, QObject* parent)
    : IMusicGateway(parent), m_client(&client)
{
    Q_ASSERT(client.thread() == thread());
    connect(&client, &SessionApiClient::completed, this,
            [this](quint64 apiId, domain::SessionToken token, const ApiResult& result) {
                const auto id = m_apiIds.value(apiId);
                const auto it = m_pending.constFind(id);
                if (it == m_pending.cend())
                    return;
                if (it->context != token || !m_client || !m_client->accepts(token))
                    finish(id, MusicFailure{MusicError::StaleContext});
                else if (it->canceled)
                    finish(id, MusicFailure{MusicError::Canceled});
                else if (const auto* user = std::get_if<domain::UserProfileQuery>(&it->query);
                         user && result.error == ApiError::None && result.decoded.businessCode &&
                         userProfileHidden(*user, *result.decoded.businessCode))
                    finish(id, domain::UserProfilePage{*user, domain::UserProfileHidden{}});
                else if (result.error != ApiError::None || result.decoded.businessCode != 200)
                    finish(id, failure(result));
                else
                    receiveSuccess(id, result.decoded.object, result.cacheRetention);
            });
    connect(&client, &QObject::destroyed, this, [this] {
        const auto ids = m_pending.keys();
        for (auto id : ids)
            finish(id, MusicFailure{MusicError::Network});
    });
}

MusicGateway::~MusicGateway()
{
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        for (auto id : m_apiIds.keys())
            m_client->cancel(id);
    }
}

quint64 MusicGateway::request(domain::SessionToken context, MusicQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto id = ++m_nextId;
    const bool busy = m_pending.size() >= 16;
    m_pending.insert(id, {context, query});
    const auto source = musicQuerySourceContext(query);
    const bool staleSource = source && *source != context;
    if (!m_client || busy || !validMusicQuery(query) || staleSource) {
        const auto error = busy          ? MusicError::Busy
                           : !m_client   ? MusicError::Network
                           : staleSource ? MusicError::StaleContext
                                         : MusicError::InvalidRequest;
        QTimer::singleShot(0, this, [this, id, error] { finish(id, MusicFailure{error}); });
        return id;
    }
    ApiOperation operation;
    QJsonObject parameters;
    if (const auto* search = std::get_if<domain::SongSearch>(&query)) {
        operation = ApiOperation::SearchSongs;
        parameters = {{QStringLiteral("keywords"), search->keywords},
                      {QStringLiteral("limit"), search->limit},
                      {QStringLiteral("offset"), search->offset}};
    } else if (const auto* details = std::get_if<DetailQuery>(&query)) {
        operation = ApiOperation::SongDetails;
        parameters = detailParameters(*details);
    } else if (const auto* lyrics = std::get_if<LyricQuery>(&query)) {
        operation = ApiOperation::SongLyrics;
        parameters.insert(QStringLiteral("id"), lyrics->song.value);
    } else if (std::holds_alternative<PlaybackLyricQuery>(query)) {
        QTimer::singleShot(0, this, [this, id] { finish(id, MusicFailure{MusicError::InvalidRequest}); });
        return id;
    } else if (std::holds_alternative<domain::DailyRecommendationsQuery>(query)) {
        operation = ApiOperation::DailyRecommendations;
    } else if (std::holds_alternative<domain::PersonalFmQuery>(query)) {
        operation = ApiOperation::PersonalFm;
    } else if (const auto* browse = std::get_if<domain::DiscoveryBrowseQuery>(&query)) {
        const auto request = discoveryBrowseRequest(*browse);
        Q_ASSERT(request);
        operation = request->operation;
        parameters = request->parameters;
    } else if (const auto* user = std::get_if<domain::UserProfileQuery>(&query)) {
        const auto request = userProfileRequest(*user);
        Q_ASSERT(request);
        operation = request->operation;
        parameters = request->parameters;
    } else if (const auto* wiki = std::get_if<domain::SongWikiTextQuery>(&query)) {
        operation = ApiOperation::SongWikiText;
        parameters = {{QStringLiteral("id"), wiki->song().value}};
    } else if (const auto history = recommendationHistoryRequest(query)) {
        operation = history->operation;
        parameters = history->parameters;
    } else if (const auto assist = searchAssistRequest(query)) {
        operation = assist->operation;
        parameters = assist->parameters;
    } else if (const auto comment = commentRequest(query)) {
        operation = comment->operation;
        parameters = comment->parameters;
    } else if (const auto podcast = podcastRequest(query)) {
        operation = podcast->operation;
        parameters = podcast->parameters;
    } else if (const auto recent = recentEntityRequest(query)) {
        operation = recent->operation;
        parameters = recent->parameters;
    } else if (const auto search = entitySearchRequest(query)) {
        operation = search->operation;
        parameters = search->parameters;
    } else if (const auto discovery = playlistDiscoveryRequest(query)) {
        operation = discovery->operation;
        parameters = discovery->parameters;
    } else if (const auto chart = chartOperation(query)) {
        operation = *chart;
    } else if (const auto history = cloudHistoryRequest(query)) {
        operation = history->operation;
        parameters = history->parameters;
    } else if (const auto library = accountLibraryRequest(query)) {
        operation = library->operation;
        parameters = library->parameters;
    } else if (const auto library = libraryRequest(query)) {
        operation = library->operation;
        parameters = library->parameters;
    } else {
        const auto catalog = catalogRequest(query);
        if (!catalog) {
            QTimer::singleShot(
                0, this, [this, id] { finish(id, MusicFailure{MusicError::InvalidRequest}); });
            return id;
        }
        operation = catalog->operation;
        parameters = catalog->parameters;
    }
    submitApi(id, operation, parameters);
    return id;
}

void MusicGateway::submitApi(quint64 id, ApiOperation operation, QJsonObject parameters)
{
    auto& pending = m_pending[id];
    m_apiIds.remove(pending.apiId);
    const auto contract = operationContract(operation, parameters);
    pending.path =
        contract ? contract->logicalPath : QStringLiteral("op#") + QString::number(int(operation));
    pending.mapped = false;
    pending.apiId = m_client->submit(pending.context, operation, std::move(parameters));
    m_apiIds.insert(pending.apiId, id);
}

void MusicGateway::receiveSuccess(quint64 id, const QJsonObject& object,
                                  domain::CacheRetention retention)
{
    m_pending[id].mapped = true;
    const auto pending = m_pending.value(id);
    const auto* query = std::get_if<domain::PlaylistSongsQuery>(&pending.query);
    if (!query) {
        auto result = mapMusicResponse(pending.query, object);
        if (auto* daily = std::get_if<domain::DailyRecommendations>(&result))
            daily->receivedAtUtc = QDateTime::currentDateTimeUtc();
        finish(id, std::move(result), retention);
        return;
    }
    if (!pending.page) {
        const auto result = mapCatalogResponse(domain::PlaylistQuery{query->playlist}, object);
        if (const auto* error = std::get_if<MusicFailure>(&result)) {
            finish(id, *error);
            return;
        }
        const auto& playlist = std::get<domain::PlaylistDetails>(result);
        if (!playlist.trackIdsComplete || !playlist.trackIds) {
            finish(id, MusicFailure{MusicError::IncompleteCatalog});
            return;
        }
        domain::PlaylistSongsPage page;
        page.query = *query;
        page.total = playlist.trackIds->size();
        page.ids = playlist.trackIds->mid(query->page.offset, query->page.limit);
        page.hasMore = query->page.offset + page.ids.size() < page.total;
        page.indexUpdatedAtMs = playlist.updatedAtMs;
        if (page.ids.isEmpty()) {
            finish(id, page);
            return;
        }
        m_pending[id].page = page;
        submitApi(id, ApiOperation::SongDetails, detailParameters(pageDetails(page)));
        return;
    }
    auto page = *pending.page;
    const auto result = mapMusicResponse(pageDetails(page), object);
    if (const auto* error = std::get_if<MusicFailure>(&result)) {
        finish(id, *error);
        return;
    }
    QHash<QString, domain::Song> byId;
    for (const auto& song : std::get<domain::SongDetails>(result).songs)
        byId.insert(song.id.value, song);
    for (const auto& song : page.ids) {
        const auto found = byId.constFind(song.value);
        if (found == byId.cend())
            page.missing.append(song);
        else
            page.songs.append(*found);
    }
    finish(id, page);
}

void MusicGateway::cancel(quint64 id)
{
    const auto it = m_pending.constFind(id);
    if (it == m_pending.cend())
        return;
    const auto apiId = it->apiId;
    m_pending[id].canceled = true; // before potentially reentrant transport abort
    if (m_client && apiId != 0)
        m_client->cancel(apiId);
    QTimer::singleShot(0, this, [this, id] { finish(id, MusicFailure{MusicError::Canceled}); });
}

void MusicGateway::finish(quint64 id, MusicResult result, domain::CacheRetention retention)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    m_apiIds.remove(pending.apiId);
    if (pending.canceled)
        result = MusicFailure{MusicError::Canceled};
    if (m_client && !m_client->accepts(pending.context))
        result = MusicFailure{MusicError::StaleContext};
    // A code-200 response the mapper would not take: the service changed its shape, or
    // the contract is too strict. The mapper may say why at debug (docs/tasks/logging.md §8).
    if (const auto* error = std::get_if<MusicFailure>(&result);
        error && error->error == MusicError::InvalidResponse && pending.mapped) {
        log::warn(lcApi, "music.invalid", {"path", pending.path});
    }
    if (std::holds_alternative<MusicFailure>(result) ||
        (!std::holds_alternative<DetailQuery>(pending.query) &&
         !std::holds_alternative<LyricQuery>(pending.query)))
        retention = {};
    emit completed(id, pending.context, std::move(result), retention);
}
} // namespace cirrus::netease
