#pragma once

#include "loadable_model.h"
#include "playlist_order_revision.h"
#include "types.h"

#include <QStringList>

namespace cirrus::ui
{

class SongDetailLoader;
class SongListModel;

// The "调整歌曲顺序" page of the account's own playlist (docs/ui/pages/playlist.md
// §13): a working copy of the song order, changed here only and saved with one
// request.
//   PlaylistOrderModel { id: model; playlistKey: key }
//   … model.moveRows([2, 3], 0); model.sortBy(PlaylistOrderModel.Title); model.save()
class PlaylistOrderModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(QString playlistKey READ playlistKey WRITE setPlaylistKey NOTIFY playlistKeyChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)
    // The order differs from the saved one.
    Q_PROPERTY(bool changed READ changed NOTIFY changedChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    // Songs whose details are on their way; a quick sort waits for them.
    Q_PROPERTY(int waiting READ waiting NOTIFY waitingChanged)
    Q_PROPERTY(bool sortPending READ sortPending NOTIFY sortPendingChanged)

  public:
    enum Field { Title, Artist, Album, Duration };
    Q_ENUM(Field)

    explicit PlaylistOrderModel(QObject* parent = nullptr);

    [[nodiscard]] QString playlistKey() const { return m_key; }
    void setPlaylistKey(const QString& key);
    [[nodiscard]] QString title() const { return m_title; }
    [[nodiscard]] SongListModel* songs() const { return m_songs; }
    [[nodiscard]] bool changed() const { return m_changed; }
    [[nodiscard]] bool saving() const { return m_saveRequest.pending(); }
    [[nodiscard]] int waiting() const;
    [[nodiscard]] bool sortPending() const { return m_pendingSort >= 0; }

    // Moves `rows` (kept in their order) into the gap before row `gap`.
    Q_INVOKABLE void moveRows(const QList<int>& rows, int gap);
    // Ascending by `field` (system collation for text, shortest first for
    // durations); songs without details last. Fetches missing details first.
    Q_INVOKABLE void sortBy(int field);
    Q_INVOKABLE void reverse();
    Q_INVOKABLE void ensureLoaded(int first, int last);
    // Restores the most recently acknowledged order before a confirmed leave.
    Q_INVOKABLE void discardChanges();
    // One request with the whole order; `saved` or `notice` follows.
    Q_INVOKABLE void save();

  signals:
    void playlistKeyChanged();
    void titleChanged();
    void changedChanged();
    void savingChanged();
    void waitingChanged();
    void sortPendingChanged();
    void saved();
    void notice(const QString& message);

  protected:
    Request load() override;
    void clear() override;

  private:
    void updateChanged();
    void applySort(int field);

    QString m_key;
    QString m_title;
    SongListModel* m_songs;
    SongDetailLoader* m_details;
    QStringList m_savedOrder;
    PlaylistOrderRevision m_revision;
    bool m_changed = false;
    int m_pendingSort = -1;
    Request m_saveRequest;
};

} // namespace cirrus::ui
