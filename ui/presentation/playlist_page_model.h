#pragma once

#include "account_membership.h"
#include "loadable_model.h"
#include "types.h"

namespace cirrus::ui
{

class SongDetailLoader;
class SongListModel;

// Playlist details page. Setting `playlistKey` loads it:
//   PlaylistPageModel { id: model; playlistKey: route.key }
//   SongTable { songs: SortedSongsModel { source: model.songs } }
// Changes to the account's own playlist show at once and are undone when saving
// fails; `notice` reports what the user should know.
class PlaylistPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(QString playlistKey READ playlistKey WRITE setPlaylistKey NOTIFY playlistKeyChanged)
    Q_PROPERTY(QString title READ title NOTIFY headerChanged)
    Q_PROPERTY(QString description READ description NOTIFY headerChanged)
    Q_PROPERTY(QString creator READ creator NOTIFY headerChanged)
    // The creator's user page; empty when unknown.
    Q_PROPERTY(QString creatorKey READ creatorKey NOTIFY headerChanged)
    Q_PROPERTY(QString coverKey READ coverKey NOTIFY headerChanged)
    Q_PROPERTY(QStringList tags READ tags NOTIFY headerChanged)
    Q_PROPERTY(QString updatedText READ updatedText NOTIFY headerChanged) // empty when unknown
    // -1 when unknown.
    Q_PROPERTY(qint64 playCount READ playCount NOTIFY headerChanged)
    Q_PROPERTY(qint64 subscriberCount READ subscriberCount NOTIFY headerChanged)
    Q_PROPERTY(qint64 commentCount READ commentCount NOTIFY headerChanged)
    Q_PROPERTY(bool ownKnown READ ownKnown NOTIFY headerChanged)
    Q_PROPERTY(bool likedListKnown READ likedListKnown NOTIFY headerChanged)
    Q_PROPERTY(bool subscribedKnown READ subscribedKnown NOTIFY subscribedChanged)
    Q_PROPERTY(bool own READ own NOTIFY headerChanged)
    // The account's "我喜欢的音乐" (own, but not renamed or deleted).
    Q_PROPERTY(bool likedList READ likedList NOTIFY headerChanged)
    // Reads are account-scoped; writes wait for confirmation and re-read membership.
    Q_PROPERTY(bool subscribed READ subscribed WRITE setSubscribed NOTIFY subscribedChanged)
    // Songs the service has no details for (large playlists fetch details by batch).
    Q_PROPERTY(int missingCount READ missingCount NOTIFY missingCountChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit PlaylistPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString playlistKey() const { return m_key; }
    void setPlaylistKey(const QString& key);
    [[nodiscard]] QString title() const { return m_info.title; }
    [[nodiscard]] QString description() const { return m_info.description; }
    [[nodiscard]] QString creator() const { return m_info.creator; }
    [[nodiscard]] QString creatorKey() const { return m_info.creatorKey; }
    [[nodiscard]] QString coverKey() const { return m_info.coverKey; }
    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] QStringList tags() const { return m_info.tags; }
    [[nodiscard]] QString updatedText() const;
    [[nodiscard]] qint64 playCount() const { return m_info.playCount; }
    [[nodiscard]] qint64 subscriberCount() const { return m_info.subscriberCount; }
    [[nodiscard]] qint64 commentCount() const { return m_info.commentCount; }
    [[nodiscard]] bool ownKnown() const { return m_membership.evidence().own.has_value(); }
    [[nodiscard]] bool likedListKnown() const { return m_membership.evidence().liked.has_value(); }
    [[nodiscard]] bool subscribedKnown() const { return m_membership.evidence().saved.has_value(); }
    [[nodiscard]] bool own() const { return m_membership.evidence().own == true; }
    [[nodiscard]] bool likedList() const { return m_membership.evidence().liked == true; }
    [[nodiscard]] bool subscribed() const { return m_membership.evidence().saved == true; }
    void setSubscribed(bool subscribed);
    [[nodiscard]] int missingCount() const;

    // Large playlists come with details for some songs only: fetches them for the
    // rows in [first, last] of `songs` (the rows on screen and around them), in
    // batches of kSongDetailsBatch, one request at a time; rows on screen go first.
    // A batch that fails is asked for again next time.
    Q_INVOKABLE void ensureLoaded(int first, int last);
    // Every song's details, for sorting and finding.
    Q_INVOKABLE void loadAll();

    // The account's own playlist.
    Q_INVOKABLE void removeSongs(const QStringList& keys);
    Q_INVOKABLE void updateInfo(const QString& title, const QString& description);
    // Emits `deleted` once the service has deleted it.
    Q_INVOKABLE void deletePlaylist();

  signals:
    void playlistKeyChanged();
    void headerChanged();
    void subscribedChanged();
    void missingCountChanged();
    // A change the account made here was saved: the library has to reload.
    void libraryChanged();
    // Something to tell the user (a change saved, or undone and why), translated.
    void notice(const QString& message);
    void deleted();

  protected:
    Request load() override;
    void clear() override;

  private:
    void restoreSongs(const QStringList& keys, const QList<SongInfo>& songs);
    [[nodiscard]] QString failureMessage(const Outcome<bool>& outcome,
                                         const QString& otherwise) const;

    QString m_key;
    PlaylistInfo m_info;
    AccountMembership m_membership;
    SongListModel* m_songs;
    Request m_subscribeRequest;
    SongDetailLoader* m_details;
    Request m_writeRequest;
};

} // namespace cirrus::ui
