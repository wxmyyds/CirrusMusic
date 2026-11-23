#pragma once

#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include "share_text.h"

// Declares one setting of AppSettings: a property `name` (READ name, WRITE setter,
// NOTIFY nameChanged) stored under `key`, `fallback` until the user changes it. The
// setter stores and notifies only on a change. Used once per setting in AppSettings;
// moc expands it like the Q_PROPERTY it contains.
#define CIRRUS_SETTING(Type, name, setter, key, fallback)                                          \
    Q_PROPERTY(Type name READ name WRITE setter NOTIFY name##Changed)                              \
                                                                                                   \
  public:                                                                                          \
    [[nodiscard]] Type name() const { return read<Type>(key, fallback); }                          \
    void setter(const Type& value)                                                                 \
    {                                                                                              \
        if (write<Type>(key, value, fallback))                                                     \
            emit name##Changed();                                                                  \
    }                                                                                              \
    Q_SIGNAL void name##Changed();                                                                 \
                                                                                                   \
  private:

namespace cirrus::ui
{

// User settings (App.settings), persisted with QSettings. Appearance values are the
// integer values of the matching Theme and Fonts enums, which Main.qml binds to them;
// this layer does not depend on the theme module.
//
// Adding a setting is one CIRRUS_SETTING line here (with what its values mean); the
// settings page shows it with a row (docs/ui/pages/settings.md §7). Keys are stored
// on disk: keep them when renaming a property.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.settings")

    // Appearance.
    // Theme.Mode: System (0), Light, Dark.
    CIRRUS_SETTING(int, themeMode, setThemeMode, "appearance/themeMode", 0)
    // Theme.SeedSource: Cover (0), Fixed, System (2, default).
    CIRRUS_SETTING(int, seedSource, setSeedSource, "appearance/seedSource", 2)
    // The seed for SeedSource Fixed (MD3's baseline purple until one is picked).
    CIRRUS_SETTING(QColor, fixedSeed, setFixedSeed, "appearance/fixedSeed", QColor(0x6750a4))
    // Theme.Variant: TonalSpot (0) …
    CIRRUS_SETTING(int, variant, setVariant, "appearance/variant", 0)
    CIRRUS_SETTING(bool, reducedMotion, setReducedMotion, "appearance/reducedMotion", false)
    // Fonts.TextSource: Bundled (0, MiSans, default), System, Custom.
    CIRRUS_SETTING(int, fontSource, setFontSource, "appearance/fontSource", 0)
    // Family for fontSource Custom.
    CIRRUS_SETTING(QString, customFontFamily, setCustomFontFamily, "appearance/customFontFamily",
                   QString())
    // Font files picked for the custom font (not installed in the system), loaded at
    // startup.
    CIRRUS_SETTING(QStringList, customFontFiles, setCustomFontFiles, "appearance/customFontFiles",
                   QStringList())

    // Window and system.
    // TitleBarMode (Cirrus.Window), or -1 until the user picks one: the default then
    // follows the desktop. Applied at startup; see docs/ui/README.md §6.1.
    CIRRUS_SETTING(int, titleBar, setTitleBar, "window/titleBar", -1)
    // What closing the main window does when a tray icon is shown: -1 ask (default),
    // 0 quit, 1 hide to the tray and keep playing. Without a tray it always quits.
    CIRRUS_SETTING(int, closeAction, setCloseAction, "window/closeAction", -1)
    // Render on the discrete GPU of a hybrid-graphics Linux machine. Applied at
    // startup, before the window exists (ui/app/gpu_preference.h).
    CIRRUS_SETTING(bool, preferDiscreteGpu, setPreferDiscreteGpu, "graphics/preferDiscreteGpu",
                   true)
    // The window as it was left: its size when not maximized, and whether it was. The
    // position is not kept (Wayland doesn't let apps place windows).
    CIRRUS_SETTING(int, windowWidth, setWindowWidth, "window/width", 1200)
    CIRRUS_SETTING(int, windowHeight, setWindowHeight, "window/height", 800)
    CIRRUS_SETTING(bool, windowMaximized, setWindowMaximized, "window/maximized", false)
    // The rail expanded in large windows (narrow ones always start collapsed).
    CIRRUS_SETTING(bool, railExpanded, setRailExpanded, "window/railExpanded", true)
    // The library and discover pages' tabs.
    CIRRUS_SETTING(int, libraryTab, setLibraryTab, "library/tab", 0)
    CIRRUS_SETTING(int, discoverTab, setDiscoverTab, "discover/tab", 0)
    // The listening statistics page: StatsPageModel.Range and rankings by time.
    CIRRUS_SETTING(int, statsRange, setStatsRange, "stats/range", 0)
    CIRRUS_SETTING(bool, statsByTime, setStatsByTime, "stats/byTime", false)

    // Playback.
    // Requested stream quality, the backend's AudioQuality: Standard (0), Exhigh (1,
    // default), Lossless, HiRes. The service answers with the best it has up to that.
    CIRRUS_SETTING(int, audioQuality, setAudioQuality, "playback/audioQuality", 1)
    CIRRUS_SETTING(bool, biliFallback, setBiliFallback, "playback/biliFallback", false)
    // The last queue always comes back at startup (docs/ui/integration.md §6 D7); this
    // also plays it on.
    CIRRUS_SETTING(bool, resumeOnStart, setResumeOnStart, "playback/resumeOnStart", false)
    // Volume normalization from the service's loudness (docs/tasks/audio-engine.md §6.5).
    CIRRUS_SETTING(bool, normalizeVolume, setNormalizeVolume, "playback/normalizeVolume", false)
    // How one track goes into the next (docs/tasks/automix.md §3), the backend's
    // TransitionMode: Smart (0, default), Crossfade, Gapless; the crossfade's length in
    // seconds (1–12).
    CIRRUS_SETTING(int, trackTransition, setTrackTransition, "playback/transition", 0)
    CIRRUS_SETTING(int, crossfadeSeconds, setCrossfadeSeconds, "playback/crossfadeSeconds", 6)
    // The player's state, applied at startup by App.player: volume (0–1, linear),
    // Playback.Order and Playback.Repeat (the same for every queue and account).
    CIRRUS_SETTING(double, volume, setVolume, "playback/volume", 1.0)
    CIRRUS_SETTING(int, playOrder, setPlayOrder, "playback/order", 0)
    CIRRUS_SETTING(int, playRepeat, setPlayRepeat, "playback/repeat", 0)

    // Lyrics.
    CIRRUS_SETTING(bool, showTranslation, setShowTranslation, "lyrics/showTranslation", true)
    CIRRUS_SETTING(bool, showRomanization, setShowRomanization, "lyrics/showRomanization", true)

    // The mini player's progress bar waves while playing (docs/ui/README.md §6.2).
    CIRRUS_SETTING(bool, miniWave, setMiniWave, "miniPlayer/wave", true)
    // The floating bar's buttons the user turned off (MiniPlayerButtonsMenu ids: like,
    // mode, volume, queue, comments); new buttons show until turned off.
    CIRRUS_SETTING(QStringList, miniPlayerHidden, setMiniPlayerHidden, "miniPlayer/hidden",
                   QStringList())
    // The mini player docked as a pane on the right of the window instead of floating
    // over the bottom of the content (docs/ui/redesign-md3e.md §11).
    CIRRUS_SETTING(bool, miniDocked, setMiniDocked, "miniPlayer/docked", false)
    // What the side player shows under its controls: 0 the lyrics, 1 the songs up next.
    CIRRUS_SETTING(int, sideSection, setSideSection, "miniPlayer/sideSection", 0)
    // Player page (docs/ui/pages/player.md §9.2).
    // Light or dark: 0 follow the appearance, 1 always dark, 2 always light.
    CIRRUS_SETTING(int, playerTheme, setPlayerTheme, "player/theme", 0)
    // The page's style: 0 the default (Apple Music-like), 1 Material (MD3 Expressive,
    // docs/ui/pages/player-expressive.md).
    CIRRUS_SETTING(int, playerStyle, setPlayerStyle, "player/style", 0)
    // The cover colors of the Material style and the mini and side players: 0 tonal
    // spot, 1 vibrant, 2 expressive, 3 fidelity (docs/ui/pages/player-expressive.md §1).
    CIRRUS_SETTING(int, playerColorStyle, setPlayerColorStyle, "player/colorStyle", 0)
    // The update check (docs/tasks/update-check.md): on its own (packaged builds only),
    // when it last asked (ms since the epoch, 0 never), the version it last announced.
    CIRRUS_SETTING(bool, updatesAuto, setUpdatesAuto, "updates/auto", true)
    CIRRUS_SETTING(qint64, updatesLastCheck, setUpdatesLastCheck, "updates/lastCheck", 0)
    CIRRUS_SETTING(QString, updatesNotified, setUpdatesNotified, "updates/notified", QString())
    // Where the controls are on wide windows: 0 under the cover, 1 at the bottom.
    CIRRUS_SETTING(int, playerControls, setPlayerControls, "player/controls", 0)
    // The title and artist: 0 under the cover, 1 in the bottom row.
    CIRRUS_SETTING(int, playerHeader, setPlayerHeader, "player/header", 0)
    // The time right of the progress bar: 0 the time left, 1 the song's length.
    CIRRUS_SETTING(int, playerTimeRight, setPlayerTimeRight, "player/timeRight", 0)
    // Seconds without input before the controls hide while playing; 0 never.
    CIRRUS_SETTING(int, playerAutoHide, setPlayerAutoHide, "player/autoHide", 3)
    // The view it opens with, the last one used: 0 cover, 1 lyrics, 2 queue.
    CIRRUS_SETTING(int, playerView, setPlayerView, "player/view", 1)
    // Background: 0 flowing light, 1 blurred cover, 2 solid, 3 still light (the flowing
    // light's look, not moving); and the flow's speed:
    // 0 slow, 1 standard, 2 fast.
    CIRRUS_SETTING(int, playerBackground, setPlayerBackground, "player/background", 0)
    CIRRUS_SETTING(int, playerFlowSpeed, setPlayerFlowSpeed, "player/flowSpeed", 1)
    // The cover tilts toward the pointer.
    CIRRUS_SETTING(bool, playerTilt, setPlayerTilt, "player/tilt", true)
    // Lyrics on the player page (player.md §5, §9.2): size in px (16-96), the gap
    // between lines in tenths of the size, weight 100-900, 0 left / 1 centered /
    // 2 right, the current line 0 upper / 1 centered, romaji as ruby over Japanese,
    // word-by-word fill, the other lines blurred, the long-note glow.
    CIRRUS_SETTING(int, lyricSize, setLyricSize, "lyrics/size", 40)
    CIRRUS_SETTING(int, lyricSpacing, setLyricSpacing, "lyrics/spacing", 9)
    CIRRUS_SETTING(int, lyricWeight, setLyricWeight, "lyrics/weight", 700)
    // The lyrics' font: 0 the UI font, 1 the bundled one, 2 lyricFontFamily.
    CIRRUS_SETTING(int, lyricFontSource, setLyricFontSource, "lyrics/fontSource", 0)
    CIRRUS_SETTING(QString, lyricFontFamily, setLyricFontFamily, "lyrics/fontFamily", QString())
    CIRRUS_SETTING(int, lyricAlign, setLyricAlign, "lyrics/align", 0)
    CIRRUS_SETTING(int, lyricAnchor, setLyricAnchor, "lyrics/anchor", 0)
    CIRRUS_SETTING(bool, lyricRuby, setLyricRuby, "lyrics/ruby", true)
    CIRRUS_SETTING(bool, lyricWordFill, setLyricWordFill, "lyrics/wordFill", true)
    CIRRUS_SETTING(bool, lyricBlur, setLyricBlur, "lyrics/blur", true)
    CIRRUS_SETTING(bool, lyricGlow, setLyricGlow, "lyrics/glow", true)

    // Recent searches, newest first (docs/ui/pages/search.md §2); only on this machine.
    CIRRUS_SETTING(QStringList, searchHistory, setSearchHistory, "search/history", QStringList())
    // What 复制链接 copies (docs/ui/pages/share.md §2): {链接} {名称} {作者} {类型}, […] left
    // out when a placeholder in it is empty.
    CIRRUS_SETTING(QString, shareTemplate, setShareTemplate, "share/template",
                   sharing::defaultTemplate())
    // The song cache's limit in bytes, every account together (Settings › Storage);
    // 0.5–50 GiB, default 2 GiB (docs/tasks/audio-engine.md §7 Q2).
    CIRRUS_SETTING(qint64, audioCacheLimit, setAudioCacheLimit, "storage/audioCacheLimit",
                   qint64(2) << 30)
    // Detailed logging is on until then (Settings › Diagnostics, docs/tasks/logging.md
    // §7); invalid or past: off.
    CIRRUS_SETTING(QDateTime, verboseLogUntil, setVerboseLogUntil, "diagnostics/verboseUntil",
                   QDateTime())

  public:
    explicit AppSettings(QObject* parent = nullptr);

  private:
    template <class T> T read(const char* key, const T& fallback) const
    {
        return m_store.value(QLatin1StringView(key), QVariant::fromValue(fallback))
            .template value<T>();
    }
    // Returns whether the stored value changed.
    template <class T> bool write(const char* key, const T& value, const T& fallback)
    {
        if (read(key, fallback) == value)
            return false;
        m_store.setValue(QLatin1StringView(key), QVariant::fromValue(value));
        return true;
    }

    QSettings m_store;
};

} // namespace cirrus::ui
