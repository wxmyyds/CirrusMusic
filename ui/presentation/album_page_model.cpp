#include "album_page_model.h"
#include "notice.h"

#include "app.h"
#include "formatter.h"
#include "song_list_model.h"

#include <QVariantMap>

namespace cirrus::ui
{

AlbumPageModel::AlbumPageModel(QObject* parent)
    : LoadableModel(parent), m_membership(this, [this] { emit subscribedChanged(); }),
      m_songs(new SongListModel(this))
{
    followAccount();
}

void AlbumPageModel::setAlbumKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    emit albumKeyChanged();
    if (key.isEmpty())
        reset();
    else
        reload();
}

QString AlbumPageModel::type() const
{
    return m_page.info.type.isEmpty() ? tr("专辑") : m_page.info.type;
}

QString AlbumPageModel::aliasText() const { return m_page.info.aliases.join(tr(" / ")); }

QVariantList AlbumPageModel::artists() const
{
    QVariantList artists;
    for (const ArtistRef& artist : m_page.info.artists)
        artists << QVariantMap{{QStringLiteral("key"), artist.key},
                               {QStringLiteral("name"), artist.name}};
    return artists;
}

QString AlbumPageModel::artistText() const { return Formatter::artists(m_page.info.artists); }

QString AlbumPageModel::releaseText() const
{
    QStringList parts;
    if (m_page.info.publishedAtMs > 0)
        parts << tr("%1发行").arg(Formatter::fullDate(m_page.info.publishedAtMs));
    else if (m_page.info.year > 0)
        parts << tr("%1年发行").arg(m_page.info.year);
    if (!m_page.info.company.isEmpty())
        parts << m_page.info.company;
    return parts.join(tr(" · "));
}

QString AlbumPageModel::countText() const
{
    if (m_page.info.key.isEmpty())
        return {};
    QStringList parts{tr("%1 首").arg(Formatter::count(m_page.songKeys.size()))};
    qint64 total = 0;
    bool known = !m_page.songs.isEmpty();
    for (const SongInfo& song : m_page.songs) {
        if (song.durationMs < 0)
            known = false;
        total += std::max<qint64>(song.durationMs, 0);
    }
    if (known)
        parts << Formatter::totalLength(total);
    return parts.join(tr(" · "));
}

void AlbumPageModel::setSubscribed(bool subscribed)
{
    if (!subscribedKnown() || subscribed == this->subscribed() || m_page.info.key.isEmpty() ||
        m_subscribeRequest.pending())
        return;
    m_membership.forgetSaved();
    m_subscribeRequest = App::instance()->librarySource()->setAlbumSubscribed(
        m_key, subscribed, m_membership.guard<bool>([this, subscribed](Outcome<bool> outcome) {
            m_subscribeRequest.release();
            refresh();
            if (outcome.value && !outcome.failure) {
                emit libraryChanged();
                return;
            }
            const bool signedOut = outcome.failure && outcome.failure->kind == failure::SignedOut;
            emit notice(signedOut ? noticed("album.subscribe_signed_out", tr("登录后才能收藏专辑"))
                        : maybeSaved(outcome)
                            ? noticed("album.subscribe_unknown", tr("可能没有保存成功，刷新看看"))
                        : subscribed
                            ? noticed("album.subscribe_failed", tr("没能收藏专辑，请稍后再试"))
                            : noticed("album.unsubscribe_failed", tr("没能取消收藏，请稍后再试")));
        }));
}

Request AlbumPageModel::load()
{
    auto* app = App::instance();
    m_membership.start(app->librarySource(), app->session(), collection::Album, m_key);
    m_subscribeRequest.cancel();
    return app->catalog()->album(
        m_key, m_membership.guard<AlbumPage>(handle<AlbumPage>([this](AlbumPage page) {
            m_page = std::move(page);
            emit headerChanged();
            emit subscribedChanged();
            m_songs->reset(m_page.songKeys, m_page.songs);
            return m_page.songKeys.isEmpty();
        })));
}

void AlbumPageModel::clear()
{
    m_membership.clear();
    m_subscribeRequest.cancel();
    m_page = {};
    emit headerChanged();
    emit subscribedChanged();
    m_songs->clear();
}

} // namespace cirrus::ui
