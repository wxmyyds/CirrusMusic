#include "taskbar_buttons.h"

#include "logging/log.h"
#include "player.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStyleHints>

#include <array>
#include <iterator>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shobjidl.h>

namespace cirrus::ui
{

namespace
{
enum Glyph { PreviousGlyph, PlayGlyph, PauseGlyph, NextGlyph };
enum Button { PreviousButton, PlayPauseButton, NextButton };
constexpr UINT kFirstId = 0x5c10; // the buttons' command ids: kFirstId + Button

// A plain glyph at `size` pixels: drawn, not from the icon font, so it is sharp at any size.
QImage glyph(Glyph kind, int size, const QColor& color)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const qreal s = size;
    const auto triangle = [&](qreal left, qreal right) { // points from left to right
        QPainterPath path;
        path.moveTo(left, 0.2 * s);
        path.lineTo(right, 0.5 * s);
        path.lineTo(left, 0.8 * s);
        path.closeSubpath();
        painter.drawPath(path);
    };
    const auto bar = [&](qreal left, qreal width) {
        painter.drawRoundedRect(QRectF(left, 0.2 * s, width, 0.6 * s), 0.04 * s, 0.04 * s);
    };
    switch (kind) {
    case PlayGlyph:
        triangle(0.3 * s, 0.8 * s);
        break;
    case PauseGlyph:
        bar(0.26 * s, 0.16 * s);
        bar(0.58 * s, 0.16 * s);
        break;
    case NextGlyph:
        triangle(0.22 * s, 0.64 * s);
        bar(0.66 * s, 0.12 * s);
        break;
    case PreviousGlyph:
        bar(0.22 * s, 0.12 * s);
        painter.translate(s, 0); // the next triangle, mirrored
        painter.scale(-1, 1);
        triangle(0.22 * s, 0.64 * s);
        break;
    }
    return image;
}

// The taskbar's own light or dark (Settings > Personalization > Colors), which can differ
// from the apps' mode.
bool lightTaskbar()
{
    const QSettings personalize(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return personalize.value(QStringLiteral("SystemUsesLightTheme"), 0).toInt() == 1;
}

void setTip(THUMBBUTTON& button, const QString& text)
{
    const QString tip = text.left(int(std::size(button.szTip)) - 1);
    const int length = tip.toWCharArray(button.szTip);
    button.szTip[length] = L'\0';
}
} // namespace

struct TaskbarButtons::Native {
    ITaskbarList3* taskbar = nullptr;
    UINT created = 0;     // the "TaskbarButtonCreated" message
    HWND added = nullptr; // the window that has the buttons
    std::array<HICON, 4> icons{};
    int iconSize = 0;
    bool iconsLight = false;

    ~Native()
    {
        destroyIcons();
        if (taskbar)
            taskbar->Release();
    }
    void destroyIcons()
    {
        for (HICON& icon : icons) {
            if (icon)
                DestroyIcon(icon);
            icon = nullptr;
        }
    }
    void makeIcons(int size, bool light)
    {
        if (size == iconSize && light == iconsLight && icons[0])
            return;
        destroyIcons();
        iconSize = size;
        iconsLight = light;
        const QColor color = light ? QColor(0, 0, 0, 230) : QColor(255, 255, 255);
        for (int kind = PreviousGlyph; kind <= NextGlyph; ++kind)
            icons[size_t(kind)] = glyph(Glyph(kind), size, color).toHICON();
    }
};

TaskbarButtons::TaskbarButtons(Player* player, QWindow* window, QObject* parent)
    : QObject(parent), m_player(player), m_window(window)
{
    auto native = std::make_unique<Native>();
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&native->taskbar));
    if (SUCCEEDED(hr))
        hr = native->taskbar->HrInit();
    if (FAILED(hr)) {
        log::warn(lcPlatform, "taskbar.unavailable", {"hresult", qint64(hr)});
        return;
    }
    native->created = RegisterWindowMessageW(L"TaskbarButtonCreated");
    m_native = std::move(native);
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(player, &Player::changed, this, [this] { update(); });
    connect(player, &Player::currentChanged, this, [this] { update(); });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { update(true); });
    // The window's taskbar button may be there already: it was shown before this was made.
    add();
}

TaskbarButtons::~TaskbarButtons()
{
    if (m_native)
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

TaskbarButtons::Shown TaskbarButtons::shown() const
{
    Shown s;
    s.song = !m_player->current().key.isEmpty();
    const int status = m_player->status();
    s.playing = status == playback::Playing || status == playback::Loading;
    s.canPrevious = m_player->canPrevious();
    s.canNext = m_player->canNext();
    s.light = lightTaskbar();
    return s;
}

namespace
{
std::array<THUMBBUTTON, 3> buttonsFor(const std::array<HICON, 4>& icons, bool song, bool playing,
                                      bool canPrevious, bool canNext)
{
    std::array<THUMBBUTTON, 3> buttons{};
    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i].dwMask = THUMBBUTTONMASK(THB_ICON | THB_TOOLTIP | THB_FLAGS);
        buttons[i].iId = kFirstId + UINT(i);
    }
    const auto flags = [](bool enabled) { return enabled ? THBF_ENABLED : THBF_DISABLED; };
    buttons[PreviousButton].hIcon = icons[PreviousGlyph];
    buttons[PreviousButton].dwFlags = flags(canPrevious);
    setTip(buttons[PreviousButton], QCoreApplication::translate("TaskbarButtons", "上一首"));
    buttons[PlayPauseButton].hIcon = playing ? icons[PauseGlyph] : icons[PlayGlyph];
    buttons[PlayPauseButton].dwFlags = flags(song);
    setTip(buttons[PlayPauseButton], playing
                                         ? QCoreApplication::translate("TaskbarButtons", "暂停")
                                         : QCoreApplication::translate("TaskbarButtons", "播放"));
    buttons[NextButton].hIcon = icons[NextGlyph];
    buttons[NextButton].dwFlags = flags(canNext);
    setTip(buttons[NextButton], QCoreApplication::translate("TaskbarButtons", "下一首"));
    return buttons;
}
} // namespace

// Once per native window: a window can be given buttons only once, and a new one (after a
// title bar mode change, or Explorer restarting) needs them again.
void TaskbarButtons::add()
{
    if (!m_native || !m_window || !m_window->handle())
        return;
    const auto hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (m_native->added == hwnd)
        return;
    // Let the taskbar's messages through even if Cirrus runs elevated.
    ChangeWindowMessageFilterEx(hwnd, m_native->created, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, WM_COMMAND, MSGFLT_ALLOW, nullptr);
    m_shown = shown();
    m_native->makeIcons(qRound(16 * m_window->devicePixelRatio()), m_shown.light);
    auto buttons = buttonsFor(m_native->icons, m_shown.song, m_shown.playing, m_shown.canPrevious,
                              m_shown.canNext);
    // Fails until the taskbar button exists; TaskbarButtonCreated then comes.
    if (SUCCEEDED(
            m_native->taskbar->ThumbBarAddButtons(hwnd, UINT(buttons.size()), buttons.data()))) {
        m_native->added = hwnd;
        log::debug(lcPlatform, "taskbar.buttons_added");
    }
}

void TaskbarButtons::update(bool force)
{
    if (!m_native || !m_window || !m_window->handle())
        return;
    const auto hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (m_native->added != hwnd) {
        add();
        return;
    }
    const Shown now = shown();
    if (!force && now == m_shown)
        return;
    m_shown = now;
    m_native->makeIcons(qRound(16 * m_window->devicePixelRatio()), now.light);
    auto buttons = buttonsFor(m_native->icons, now.song, now.playing, now.canPrevious, now.canNext);
    m_native->taskbar->ThumbBarUpdateButtons(hwnd, UINT(buttons.size()), buttons.data());
}

bool TaskbarButtons::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (!m_native || eventType != "windows_generic_MSG" || !m_window || !m_window->handle())
        return false;
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->hwnd != reinterpret_cast<HWND>(m_window->winId()))
        return false;
    if (msg->message == m_native->created) {
        // A new taskbar button (shown again, or Explorer restarted): add the buttons anew.
        m_native->added = nullptr;
        QMetaObject::invokeMethod(this, [this] { add(); }, Qt::QueuedConnection);
        return false;
    }
    if (msg->message != WM_COMMAND || HIWORD(msg->wParam) != THBN_CLICKED)
        return false;
    const UINT id = LOWORD(msg->wParam);
    if (id < kFirstId || id > kFirstId + NextButton)
        return false;
    // Out of the native message handling first, as a click on the window's buttons.
    const auto button = Button(id - kFirstId);
    QMetaObject::invokeMethod(
        this,
        [this, button] {
            switch (button) {
            case PreviousButton:
                m_player->previous();
                break;
            case PlayPauseButton:
                m_player->playPause();
                break;
            case NextButton:
                m_player->next();
                break;
            }
        },
        Qt::QueuedConnection);
    if (result)
        *result = 0;
    return true;
}

} // namespace cirrus::ui
