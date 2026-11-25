#pragma once

#include <QObject>

#include <memory>

class QLocalServer;
class QLocalSocket;
class QLockFile;

namespace cirrus::ui
{

// One running app per user (docs/10-linux-integration.md §7): a lock file and a local
// socket in a private directory under XDG_RUNTIME_DIR. A second launch asks the
// running app to show its window and exits. The messages are
//   activate [<xdg-activation-token>]\n
//   open <xdg-activation-token or -> <orpheus link>\n
// the second for a link the browser launched it with (docs/ui/pages/open-links.md),
// checked again (applink::plausible) on arrival; nothing else can be passed in.
class SingleInstance : public QObject
{
    Q_OBJECT

  public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    // True when this process should run: it holds the lock and listens, or the lock's
    // owner does not answer (a stuck lock never blocks starting). False when the
    // running app took the activation; this process should exit.
    // `link`: an orpheus:// launch argument, passed on to the running app (else empty).
    [[nodiscard]] bool claim(const QString& link = {});

  signals:
    // Another launch wants the window. `token` lets Wayland raise it; may be empty.
    void activateRequested(const QString& token);
    // …and wants it to open this orpheus:// link (after activateRequested).
    void linkRequested(const QString& link);

  private:
    void listen(const QString& name);
    void read(QLocalSocket* socket);

    std::unique_ptr<QLockFile> m_lock;
    QLocalServer* m_server = nullptr;
};

} // namespace cirrus::ui
