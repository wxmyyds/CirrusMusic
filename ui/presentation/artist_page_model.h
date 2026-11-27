#pragma once

#include "account_membership.h"
#include "loadable_model.h"
#include "paged_model.h"
#include "types.h"

#include <QVariantList>

namespace cirrus::ui
{

class CollectionListModel;
class SongListModel;

// An artist's songs, a page at a time, most popular or newest first
// (docs/ui/pages/artist.md §3.1). Owned by ArtistPageModel.
class ArtistSongsModel : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use ArtistPageModel.songs")
    // Newest first instead of most popular; changing it loads from the start.
    Q_PROPERTY(bool byTime READ byTime WRITE setByTime NOTIFY byTimeChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit ArtistSongsModel(QObject* parent = nullptr);

    [[nodiscard]] bool byTime() const { return m_byTime; }
    void setByTime(bool byTime);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    void setArtistKey(const QString& key);
    // Loads the first page unless it has been.
    Q_INVOKABLE void ensureLoaded();

  signals:
    void byTimeChanged();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    QString m_key;
    bool m_byTime = false;
    SongListModel* m_songs;
};

// An artist's albums, a page at a time.
class ArtistAlbumsModel : public PagedModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use ArtistPageModel.albums")
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    explicit ArtistAlbumsModel(QObject* parent = nullptr);

    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    void setArtistKey(const QString& key);
    Q_INVOKABLE void ensureLoaded();

  protected:
    Request load() override { return loadFirst(); }
    void clear() override;
    Request loadPage(PageRequest page) override;
    [[nodiscard]] int loadedCount() const override;

  private:
    QString m_key;
    CollectionListModel* m_items;
};

// An artist's introduction (the 详情 tab).
class ArtistIntroductionModel : public LoadableModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use ArtistPageModel.introduction")
    Q_PROPERTY(QString brief READ brief NOTIFY contentChanged)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY contentChanged) // [{title, text}]

  public:
    explicit ArtistIntroductionModel(QObject* parent = nullptr);

    [[nodiscard]] QString brief() const { return m_intro.brief; }
    [[nodiscard]] QVariantList sections() const;
    void setArtistKey(const QString& key);
    Q_INVOKABLE void ensureLoaded();

  signals:
    void contentChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    ArtistIntroduction m_intro;
};

// Artists like this one.
class SimilarArtistsModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("collection_list_model.h")
    QML_ELEMENT
    QML_UNCREATABLE("Use ArtistPageModel.similar")
    Q_PROPERTY(cirrus::ui::CollectionListModel* items READ items CONSTANT)

  public:
    explicit SimilarArtistsModel(QObject* parent = nullptr);

    [[nodiscard]] CollectionListModel* items() const { return m_items; }
    void setArtistKey(const QString& key);
    Q_INVOKABLE void ensureLoaded();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    CollectionListModel* m_items;
};

// Artist page (docs/ui/pages/artist.md): the header and popular songs; each tab's
// content has its own model, loaded when the tab is first shown. Setting `artistKey`
// loads it.
class ArtistPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(QString artistKey READ artistKey WRITE setArtistKey NOTIFY artistKeyChanged)
    Q_PROPERTY(QString name READ name NOTIFY headerChanged)
    // Other names, " / " between them.
    Q_PROPERTY(QString aliasText READ aliasText NOTIFY headerChanged)
    Q_PROPERTY(QString description READ description NOTIFY headerChanged)
    Q_PROPERTY(QString avatarKey READ avatarKey NOTIFY headerChanged)
    // The musician's own user page; empty when unknown.
    Q_PROPERTY(QString userKey READ userKey NOTIFY headerChanged)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY headerChanged) // -1 when unknown
    // "单曲 86 · 专辑 40", leaving out unknown counts.
    Q_PROPERTY(QString countText READ countText NOTIFY headerChanged)
    // Account library evidence; writes wait for confirmation and re-read membership.
    Q_PROPERTY(bool followedKnown READ followedKnown NOTIFY followedChanged)
    Q_PROPERTY(bool followed READ followed WRITE setFollowed NOTIFY followedChanged)
    // The popular songs (at most 50), for "播放热门歌曲".
    Q_PROPERTY(cirrus::ui::SongListModel* topSongs READ topSongs CONSTANT)
    Q_PROPERTY(cirrus::ui::ArtistSongsModel* songs READ songs CONSTANT)
    Q_PROPERTY(cirrus::ui::ArtistAlbumsModel* albums READ albums CONSTANT)
    Q_PROPERTY(cirrus::ui::ArtistIntroductionModel* introduction READ introduction CONSTANT)
    Q_PROPERTY(cirrus::ui::SimilarArtistsModel* similar READ similar CONSTANT)

  public:
    explicit ArtistPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString artistKey() const { return m_key; }
    void setArtistKey(const QString& key);
    [[nodiscard]] QString name() const { return m_info.name; }
    [[nodiscard]] QString aliasText() const;
    [[nodiscard]] QString description() const { return m_info.description; }
    [[nodiscard]] QString avatarKey() const { return m_info.avatarKey; }
    [[nodiscard]] QString userKey() const { return m_info.userKey; }
    [[nodiscard]] int albumCount() const { return m_info.albumCount; }
    [[nodiscard]] QString countText() const;
    [[nodiscard]] bool followedKnown() const { return m_membership.evidence().saved.has_value(); }
    [[nodiscard]] bool followed() const { return m_membership.evidence().saved == true; }
    void setFollowed(bool followed);
    [[nodiscard]] SongListModel* topSongs() const { return m_topSongs; }
    [[nodiscard]] ArtistSongsModel* songs() const { return m_songs; }
    [[nodiscard]] ArtistAlbumsModel* albums() const { return m_albums; }
    [[nodiscard]] ArtistIntroductionModel* introduction() const { return m_introduction; }
    [[nodiscard]] SimilarArtistsModel* similar() const { return m_similar; }

  signals:
    void artistKeyChanged();
    void headerChanged();
    void followedChanged();
    // A short message for the user (a failed follow).
    void notice(const QString& message);
    // The account's followed artists changed.
    void libraryChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    ArtistInfo m_info;
    AccountMembership m_membership;
    SongListModel* m_topSongs;
    ArtistSongsModel* m_songs;
    ArtistAlbumsModel* m_albums;
    ArtistIntroductionModel* m_introduction;
    SimilarArtistsModel* m_similar;
    Request m_followRequest;
};

} // namespace cirrus::ui
