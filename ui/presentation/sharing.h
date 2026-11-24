#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{
class AppSettings;

// Copying an item's link as the user's share text (App.share, docs/ui/pages/share.md).
class Share : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.share")
    Q_PROPERTY(QString defaultTemplate READ defaultTemplate CONSTANT)

  public:
    // As sharing::Kind.
    enum Kind { Song, Playlist, Album, Artist, Podcast, Voice, User };
    Q_ENUM(Kind)

    explicit Share(AppSettings* settings, QObject* parent = nullptr);

    [[nodiscard]] QString defaultTemplate() const;
    // Puts the share text for it on the clipboard (the template in the settings) and says
    // so (notice). `author`: a song's or album's artists, a playlist's creator, a voice's
    // podcast; empty for the rest. False (nothing copied) when `key` is no id.
    Q_INVOKABLE bool copy(int kind, const QString& key, const QString& title,
                          const QString& author);
    // What `templ` makes of it (the settings' preview).
    Q_INVOKABLE QString text(const QString& templ, int kind, const QString& key,
                             const QString& title, const QString& author) const;

  signals:
    void notice(const QString& message);

  private:
    AppSettings* m_settings;
};
} // namespace cirrus::ui
