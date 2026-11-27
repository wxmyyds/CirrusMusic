#include "shortcut_map.h"

#include "logging/categories.h"
#include "logging/log.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QQuickItem>
#include <QQuickWindow>

#include <optional>

namespace cirrus::ui
{

namespace
{
const QString kGroup = QStringLiteral("shortcuts/");

QString portable(const QKeySequence& sequence)
{
    return sequence.toString(QKeySequence::PortableText);
}

// One key combination, or nothing (empty, several chords, unparsable).
std::optional<QKeyCombination> combinationOf(const QString& sequence)
{
    const QKeySequence parsed = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    if (parsed.count() != 1 || parsed[0].key() == Qt::Key_unknown)
        return std::nullopt;
    return parsed[0];
}

// The same key, written one way.
QString normalized(const QString& sequence)
{
    const auto combination = combinationOf(sequence);
    return combination ? portable(QKeySequence(*combination)) : QString();
}

// The platform's bindings of a standard key, then `extra`, without repeats, at most two.
QStringList bindings(QKeySequence::StandardKey key, const QStringList& extra = {})
{
    QStringList out;
    const auto add = [&out](const QString& sequence) {
        const QString text = normalized(sequence);
        if (!text.isEmpty() && !out.contains(text) && out.size() < ShortcutMap::MaximumKeys)
            out << text;
    };
    for (const QKeySequence& sequence : QKeySequence::keyBindings(key))
        add(portable(sequence));
    for (const QString& sequence : extra)
        add(sequence);
    return out;
}

QStringList plain(const QStringList& sequences)
{
    QStringList out;
    for (const QString& sequence : sequences)
        out << normalized(sequence);
    return out;
}

bool isModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case 0:
    case Qt::Key_unknown:
        return true;
    default:
        return false;
    }
}

constexpr Qt::KeyboardModifiers kCommandModifiers =
    Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
} // namespace

ShortcutMap::ShortcutMap(QObject* parent) : QObject(parent)
{
    // The order of the settings page. Defaults: what the app has always had, written once
    // here (docs/ui/pages/shortcuts.md §1).
    m_actions = {
        {QStringLiteral("playPause"), tr("播放／暂停"), Playback, plain({"Ctrl+P"})},
        {QStringLiteral("previous"), tr("上一首"), Playback, plain({"Ctrl+Left"})},
        {QStringLiteral("next"), tr("下一首"), Playback, plain({"Ctrl+Right"})},
        {QStringLiteral("volumeUp"), tr("音量加"), Playback, plain({"Ctrl+Up"})},
        {QStringLiteral("volumeDown"), tr("音量减"), Playback, plain({"Ctrl+Down"})},
        {QStringLiteral("like"), tr("喜欢正在播放的歌曲"), Playback, plain({"Ctrl+L"})},
        {QStringLiteral("togglePlayer"), tr("打开／关闭播放页"), Playback, plain({"Ctrl+Shift+F"})},
        {QStringLiteral("search"), tr("搜索"), Anywhere, plain({"Ctrl+K"})},
        {QStringLiteral("find"), tr("在页面里查找"), Anywhere, bindings(QKeySequence::Find)},
        {QStringLiteral("back"), tr("后退"), Anywhere, bindings(QKeySequence::Back)},
        {QStringLiteral("forward"), tr("前进"), Anywhere, bindings(QKeySequence::Forward)},
        {QStringLiteral("copyLink"), tr("复制链接"), Anywhere, plain({"Ctrl+Shift+C"})},
        {QStringLiteral("settings"), tr("设置"), Anywhere,
         bindings(QKeySequence::Preferences, {"Ctrl+,"})},
        {QStringLiteral("quit"), tr("退出"), Anywhere, bindings(QKeySequence::Quit, {"Ctrl+Q"})},
        // Shift+Tab arrives as Backtab (with Shift) on every platform.
        {QStringLiteral("previousTab"), tr("上一个标签"), Pages,
         plain({"Ctrl+PgUp", "Ctrl+Shift+Backtab"})},
        {QStringLiteral("nextTab"), tr("下一个标签"), Pages, plain({"Ctrl+PgDown", "Ctrl+Tab"})},
        {QStringLiteral("refresh"), tr("刷新首页"), Pages, bindings(QKeySequence::Refresh)},
        {QStringLiteral("statsPrevious"), tr("听歌统计的上一段"), Pages, plain({"["})},
        {QStringLiteral("statsNext"), tr("听歌统计的下一段"), Pages, plain({"]"})},
        {QStringLiteral("saveOrder"), tr("保存歌曲顺序"), Pages, bindings(QKeySequence::Save)},
    };
    for (const Action& action : m_actions) {
        const QString key = kGroup + action.id;
        if (!m_store.contains(key)) {
            m_keys.insert(action.id, action.defaults);
            continue;
        }
        // Changed ones; an empty list is kept as one empty string (no keys at all).
        QStringList keys;
        for (const QString& sequence : m_store.value(key).toStringList()) {
            const QString text = normalized(sequence);
            if (!text.isEmpty() && !reserved(text) && !keys.contains(text) &&
                keys.size() < MaximumKeys)
                keys << text;
        }
        m_keys.insert(action.id, keys);
    }
    if (auto* app = QCoreApplication::instance())
        app->installEventFilter(this);
}

ShortcutMap::~ShortcutMap()
{
    if (auto* app = QCoreApplication::instance())
        app->removeEventFilter(this);
}

const ShortcutMap::Action* ShortcutMap::find(const QString& id) const
{
    for (const Action& action : m_actions)
        if (action.id == id)
            return &action;
    return nullptr;
}

QVariantMap ShortcutMap::keys() const
{
    QVariantMap out;
    for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QVariantList ShortcutMap::actions() const
{
    QVariantList out;
    for (const Action& action : m_actions)
        out << QVariantMap{{QStringLiteral("id"), action.id},
                           {QStringLiteral("text"), action.text},
                           {QStringLiteral("group"), int(action.group)},
                           {QStringLiteral("defaults"), action.defaults}};
    return out;
}

QVariantMap ShortcutMap::hints() const
{
    QVariantMap out;
    for (auto it = m_keys.cbegin(); it != m_keys.cend(); ++it)
        out.insert(it.key(),
                   it.value().isEmpty() ? QString() : displayText(it.value().constFirst()));
    return out;
}

bool ShortcutMap::customized() const
{
    for (const Action& action : m_actions)
        if (m_keys.value(action.id) != action.defaults)
            return true;
    return false;
}

QString ShortcutMap::title(const QString& id) const
{
    const Action* action = find(id);
    return action ? action->text : QString();
}

QString ShortcutMap::sequenceOf(int key, int modifiers) const
{
    if (isModifierKey(key))
        return {};
    auto mods = Qt::KeyboardModifiers(modifiers) & (kCommandModifiers | Qt::ShiftModifier);
    // A printed symbol already says whether Shift was down ("!" is Shift+1): Shift stays
    // only with letters and the keys without a symbol of their own (Tab, F1, arrows…).
    const bool letter = key >= Qt::Key_A && key <= Qt::Key_Z;
    const bool symbol = key > Qt::Key_Space && key < 0x01000000 && !letter;
    if (symbol)
        mods &= ~Qt::ShiftModifier;
    return portable(QKeySequence(QKeyCombination(mods, Qt::Key(key))));
}

bool ShortcutMap::reserved(const QString& sequence) const
{
    const auto combination = combinationOf(sequence);
    if (!combination)
        return true;
    switch (combination->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Menu:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        // With Ctrl, Alt or Meta they are free (Ctrl+←, Ctrl+Tab, Ctrl+PgUp).
        return !(combination->keyboardModifiers() & kCommandModifiers);
    default:
        return isModifierKey(combination->key());
    }
}

QString ShortcutMap::owner(const QString& sequence, const QString& exceptId) const
{
    const QString text = normalized(sequence);
    if (text.isEmpty())
        return {};
    for (const Action& action : m_actions)
        if (action.id != exceptId && m_keys.value(action.id).contains(text))
            return action.id;
    return {};
}

void ShortcutMap::assign(const QString& id, int index, const QString& sequence)
{
    const QString text = normalized(sequence);
    if (!find(id) || text.isEmpty() || reserved(text) || index < 0 || index >= MaximumKeys)
        return;
    // Taken from wherever it was, this action's other slot included.
    for (const Action& action : m_actions) {
        if (action.id == id)
            continue;
        QStringList& keys = m_keys[action.id];
        if (keys.removeAll(text) > 0) {
            store(action.id);
            log::info(lcUi, "shortcut.taken", {{"from", action.id}, {"to", id}});
        }
    }
    QStringList keys = m_keys.value(id);
    if (index < keys.size())
        keys[index] = text;
    else
        keys << text;
    keys.removeDuplicates();
    while (keys.size() > MaximumKeys)
        keys.removeLast();
    m_keys.insert(id, keys);
    store(id);
    log::info(lcUi, "shortcut.assigned", {{"action", id}, {"slot", index}});
    emit changed();
}

void ShortcutMap::clear(const QString& id, int index)
{
    QStringList keys = m_keys.value(id);
    if (!find(id) || index < 0 || index >= keys.size())
        return;
    keys.removeAt(index);
    m_keys.insert(id, keys);
    store(id);
    log::info(lcUi, "shortcut.cleared", {{"action", id}, {"slot", index}});
    emit changed();
}

void ShortcutMap::reset(const QString& id)
{
    const Action* action = find(id);
    if (!action || m_keys.value(id) == action->defaults)
        return;
    // Its defaults come back; another action that had taken one gives it up.
    for (const QString& text : action->defaults) {
        for (const Action& other : m_actions) {
            if (other.id != id && m_keys[other.id].removeAll(text) > 0)
                store(other.id);
        }
    }
    m_keys.insert(id, action->defaults);
    store(id);
    log::info(lcUi, "shortcut.reset", {{"action", id}});
    emit changed();
}

void ShortcutMap::resetAll()
{
    for (const Action& action : m_actions) {
        m_keys.insert(action.id, action.defaults);
        m_store.remove(kGroup + action.id);
    }
    log::info(lcUi, "shortcut.reset_all");
    emit changed();
}

void ShortcutMap::store(const QString& id)
{
    const Action* action = find(id);
    if (!action)
        return;
    const QStringList keys = m_keys.value(id);
    if (keys == action->defaults)
        m_store.remove(kGroup + id);
    else
        m_store.setValue(kGroup + id, keys.isEmpty() ? QStringList{QString()} : keys);
}

QString ShortcutMap::displayText(const QString& sequence) const
{
    const auto combination = combinationOf(sequence);
    if (!combination)
        return {};
    const QString full = QKeySequence(*combination).toString(QKeySequence::NativeText);
    const QString key = QKeySequence(QKeyCombination(Qt::NoModifier, combination->key()))
                            .toString(QKeySequence::NativeText);
    if (!full.endsWith(key))
        return full;
    QString shown = key;
    switch (combination->key()) {
    case Qt::Key_Left:
        shown = QStringLiteral("←");
        break;
    case Qt::Key_Right:
        shown = QStringLiteral("→");
        break;
    case Qt::Key_Up:
        shown = QStringLiteral("↑");
        break;
    case Qt::Key_Down:
        shown = QStringLiteral("↓");
        break;
    case Qt::Key_Backtab:
        shown = QStringLiteral("Tab"); // Shift is already in the modifiers
        break;
    case Qt::Key_Space:
        shown = tr("空格");
        break;
    default:
        break;
    }
    return full.left(full.size() - key.size()) + shown;
}

bool ShortcutMap::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::ShortcutOverride)
        return false;
    auto* item = qobject_cast<QQuickItem*>(watched);
    if (!item || !item->window() || item->window()->activeFocusItem() != item)
        return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->modifiers() & kCommandModifiers)
        return false;
    // Typing in an editable field: keys that type something are text, not shortcuts
    // (F5 or a media key still reach theirs).
    const QString text = key->text();
    const bool editable = (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit")) &&
                          !item->property("readOnly").toBool() && !text.isEmpty() &&
                          text.front().isPrint();
    // Space presses a focused button (switch, check box, row).
    const bool pressesButton =
        key->key() == Qt::Key_Space && item->inherits("QQuickAbstractButton");
    if (!editable && !pressesButton)
        return false;
    key->accept();
    return true;
}

} // namespace cirrus::ui
