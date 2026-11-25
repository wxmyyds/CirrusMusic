#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QWindow>

#include <memory>

namespace cirrus::ui
{

class Player;

// Windows: previous, play/pause and next under the window's taskbar thumbnail
// (ITaskbarList3 thumbnail toolbar), driven by App.player like the window's own buttons
// (docs/ui/pages/shell.md §7). Does nothing when the taskbar is unavailable. Not yet
// verified on Windows.
class TaskbarButtons : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
  public:
    TaskbarButtons(Player* player, QWindow* window, QObject* parent = nullptr);
    ~TaskbarButtons() override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

  private:
    struct Native;
    // What the buttons show; they are updated only when it changes.
    struct Shown {
        bool song = false, playing = false, canPrevious = false, canNext = false;
        bool light = false; // icons for a light taskbar
        friend bool operator==(const Shown&, const Shown&) = default;
    };

    void add();
    void update(bool force = false);
    [[nodiscard]] Shown shown() const;

    Player* m_player;
    QPointer<QWindow> m_window;
    std::unique_ptr<Native> m_native;
    Shown m_shown;
};

} // namespace cirrus::ui
