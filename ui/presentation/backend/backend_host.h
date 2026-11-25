#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace cirrus::application
{
class ListeningReporter;
class TogetherSession;
} // namespace cirrus::application
namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class NowPlayingExport;

// Owns the backend (BackendRuntime) for the app: where its files live, starting it,
// and closing it before the app quits (docs/ui/integration.md §3). The backend
// sources (UI phase 5) are made from its runtime.
class BackendHost : public QObject
{
    Q_OBJECT

  public:
    struct Paths {
        // The installation database (account records, device identity).
        QString database;
        // The resource cache (covers, song details, lyrics): a private directory.
        QString cache;
        QString keychainService;
        // The audio cache (played songs, ADR-0008): a private directory.
        QString audioCache;
    };
    // The app's directories (Cirrus, com.guang233.Cirrus), created private (0700);
    // nullopt with `error` set when they cannot be made.
    static std::optional<Paths> preparePaths(QString* error);

    explicit BackendHost(const Paths& paths, QObject* parent = nullptr);
    ~BackendHost() override;

    [[nodiscard]] runtime::BackendRuntime& runtime() const { return *m_runtime; }
    // Reports what was listened to (integration.md §6.1); settled before closing.
    [[nodiscard]] application::ListeningReporter& reporter() const { return *m_reporter; }
    // Listen together (docs/api/together-polling.md); closing keeps its room (resume()).
    [[nodiscard]] application::TogetherSession& together() const { return *m_together; }
    // What is playing, for other programs (Linux; off unless the user turns it on).
    [[nodiscard]] NowPlayingExport& nowPlaying() const { return *m_nowPlaying; }

    void start();
    // Closes the backend (it never signs out); `done` runs once it has stopped, or
    // when it still hasn't some seconds after reporting the delay (a keyring that
    // never answers, integration.md §6 D6). Runs `done` at most once.
    void shutdown(std::function<void()> done);

  private:
    void finish();

    std::unique_ptr<runtime::BackendRuntime> m_runtime;
    std::unique_ptr<application::ListeningReporter> m_reporter;
    std::unique_ptr<application::TogetherSession> m_together;
    std::unique_ptr<NowPlayingExport> m_nowPlaying;
    std::function<void()> m_done;
    bool m_shuttingDown = false;
};

} // namespace cirrus::ui
