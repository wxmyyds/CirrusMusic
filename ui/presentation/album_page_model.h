#pragma once

#include "account_membership.h"
#include "loadable_model.h"
#include "types.h"

#include <QVariantList>

namespace cirrus::ui
{

class SongListModel;

// Album details page (docs/ui/pages/album-daily.md §1). Setting `albumKey` loads it.
// Partial when the source reports the track list as incomplete.
class AlbumPageModel : public LoadableModel
{
    Q_OBJECT
    Q_MOC_INCLUDE("song_list_model.h")
    QML_ELEMENT
    Q_PROPERTY(QString albumKey READ albumKey WRITE setAlbumKey NOTIFY albumKeyChanged)
    Q_PROPERTY(QString title READ title NOTIFY headerChanged)
    // The service's type (专辑, EP, 单曲…), "专辑" when it gives none.
    Q_PROPERTY(QString type READ type NOTIFY headerChanged)
    // Other names, " / " between them; empty when none.
    Q_PROPERTY(QString aliasText READ aliasText NOTIFY headerChanged)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY headerChanged) // [{key, name}]
    Q_PROPERTY(QString artistText READ artistText NOTIFY headerChanged)
    Q_PROPERTY(QString coverKey READ coverKey NOTIFY headerChanged)
    Q_PROPERTY(QString description READ description NOTIFY headerChanged)
    // "2024年5月1日发行 · 唱片公司", leaving out what is unknown.
    Q_PROPERTY(QString releaseText READ releaseText NOTIFY headerChanged)
    // "12 首 · 48 分钟"; the length only when every song's is known.
    Q_PROPERTY(QString countText READ countText NOTIFY headerChanged)
    Q_PROPERTY(qint64 commentCount READ commentCount NOTIFY headerChanged) // -1 when unknown
    // Account library evidence; writes wait for confirmation and re-read membership.
    Q_PROPERTY(bool subscribedKnown READ subscribedKnown NOTIFY subscribedChanged)
    Q_PROPERTY(bool subscribed READ subscribed WRITE setSubscribed NOTIFY subscribedChanged)
    Q_PROPERTY(cirrus::ui::SongListModel* songs READ songs CONSTANT)

  public:
    explicit AlbumPageModel(QObject* parent = nullptr);

    [[nodiscard]] QString albumKey() const { return m_key; }
    void setAlbumKey(const QString& key);
    [[nodiscard]] QString title() const { return m_page.info.title; }
    [[nodiscard]] QString type() const;
    [[nodiscard]] QString aliasText() const;
    [[nodiscard]] QVariantList artists() const;
    [[nodiscard]] QString artistText() const;
    [[nodiscard]] QString coverKey() const { return m_page.info.coverKey; }
    [[nodiscard]] QString description() const { return m_page.info.description; }
    [[nodiscard]] QString releaseText() const;
    [[nodiscard]] QString countText() const;
    [[nodiscard]] qint64 commentCount() const { return m_page.info.commentCount; }
    [[nodiscard]] bool subscribedKnown() const { return m_membership.evidence().saved.has_value(); }
    [[nodiscard]] bool subscribed() const { return m_membership.evidence().saved == true; }
    void setSubscribed(bool subscribed);
    [[nodiscard]] SongListModel* songs() const { return m_songs; }

  signals:
    void albumKeyChanged();
    void headerChanged();
    void subscribedChanged();
    // A short message for the user (a failed save).
    void notice(const QString& message);
    // The account's saved albums changed.
    void libraryChanged();

  protected:
    Request load() override;
    void clear() override;

  private:
    QString m_key;
    AlbumPage m_page;
    AccountMembership m_membership;
    SongListModel* m_songs;
    Request m_subscribeRequest;
};

} // namespace cirrus::ui
