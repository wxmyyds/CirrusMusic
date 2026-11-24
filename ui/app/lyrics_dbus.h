#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

namespace cirrus::ui
{
class AppSettings;
class CurrentLyrics;
class Mpris;

// Opt-in, read-only session-bus extension; independent of the standard MPRIS object.
class LyricsDBus : public QObject
{
    Q_OBJECT
  public:
    LyricsDBus(Mpris* mpris, CurrentLyrics* lyrics, AppSettings* settings);
    ~LyricsDBus() override;
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] QVariantMap document() const { return m_document; }
    [[nodiscard]] QVariantMap state() const;
    [[nodiscard]] QVariantMap snapshot() const;

  private:
    void refreshDocument();
    void refreshState(bool force = false);
    void updateTimer();
    void notify(const QVariantMap& changed);
    [[nodiscard]] QVariantMap buildDocument() const;
    Mpris* m_mpris;
    CurrentLyrics* m_lyrics;
    AppSettings* m_settings;
    bool m_registered = false;
    bool m_wasEnabled = false;
    quint64 m_revision = 0;
    QVariantMap m_document;
    QVariantMap m_sentState;
    QTimer m_timer;
};

class LyricsDBusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.cirrus.Lyrics1")
    Q_PROPERTY(uint Version READ version CONSTANT)
    Q_PROPERTY(bool Enabled READ enabled)
    Q_PROPERTY(QVariantMap Document READ document)
    Q_PROPERTY(QVariantMap State READ state)
  public:
    explicit LyricsDBusAdaptor(LyricsDBus* owner) : QDBusAbstractAdaptor(owner), m_owner(owner) {}
    [[nodiscard]] uint version() const { return 1; }
    [[nodiscard]] bool enabled() const { return m_owner->enabled(); }
    [[nodiscard]] QVariantMap document() const { return m_owner->document(); }
    [[nodiscard]] QVariantMap state() const { return m_owner->state(); }
  public slots:
    QVariantMap GetSnapshot() { return m_owner->snapshot(); }
  private:
    LyricsDBus* m_owner;
};
} // namespace cirrus::ui
