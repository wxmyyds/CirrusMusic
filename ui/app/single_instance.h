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
    enum class ClaimResult { Started, Activated, Failed };

    explicit SingleInstance(QObject* parent = nullptr);
    // An explicit private runtime directory also permits isolated instance coordination.
    SingleInstance(QString runtimeDirectory, QObject* parent = nullptr);
    ~SingleInstance() override;

    // Started holds the lock and listens; Activated means a running instance accepted the
    // activation write; Failed means neither outcome could be established.
    // `link`: an orpheus:// launch argument, passed on to the running app (else empty).
    [[nodiscard]] ClaimResult claim(const QString& link = {});

  signals:
    // Another launch wants the window. `token` lets Wayland raise it; may be empty.
    void activateRequested(const QString& token);
    // …and wants it to open this orpheus:// link (after activateRequested).
    void linkRequested(const QString& link);

  private:
    bool listen(const QString& name);
    void read(QLocalSocket* socket);

    QString m_runtimeDirectory;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer* m_server = nullptr;
};

} // namespace cirrus::ui
