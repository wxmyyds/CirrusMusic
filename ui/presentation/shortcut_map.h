#pragma once

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace cirrus::ui
{

// The keyboard shortcuts the user can change (App.shortcuts; docs/ui/pages/shortcuts.md).
// Each action has up to two key sequences, its defaults until changed; QML Shortcuts take
// theirs from `keys`:
//   Shortcut { sequences: App.shortcuts.keys.playPause; onActivated: … }
// Changes are kept in the settings (shortcuts/<id>), only where they differ from the
// defaults, so changed defaults reach everyone who kept them.
//
// It also keeps single keys (no Ctrl, Alt or Meta) from firing a shortcut while they are
// needed where the focus is: in a text field, and Space on a button (an application event
// filter). Lists that take typed text do the same themselves (SongTable).
class ShortcutMap : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.shortcuts")
    // Action id -> its key sequences (QKeySequence portable text), for Shortcut.sequences.
    Q_PROPERTY(QVariantMap keys READ keys NOTIFY changed)
    // The actions in their order: [{ id, text, group, defaults }] (group: ShortcutMap.Group).
    Q_PROPERTY(QVariantList actions READ actions CONSTANT)
    // Action id -> its first key as printed ("Ctrl+K"), empty without keys: for hints.
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY changed)
    // Whether any action differs from its defaults.
    Q_PROPERTY(bool customized READ customized NOTIFY changed)

  public:
    static constexpr int MaximumKeys = 2;
    enum Group { Playback, Anywhere, Pages };
    Q_ENUM(Group)

    explicit ShortcutMap(QObject* parent = nullptr);
    ~ShortcutMap() override;

    [[nodiscard]] QVariantMap keys() const;
    [[nodiscard]] QVariantList actions() const;
    [[nodiscard]] QVariantMap hints() const;
    [[nodiscard]] bool customized() const;

    Q_INVOKABLE QString title(const QString& id) const;
    // A pressed key as a sequence ("Ctrl+Shift+K"); empty while only modifiers are down.
    Q_INVOKABLE QString sequenceOf(int key, int modifiers) const;
    // Keys the keyboard needs to stay usable, which no action may take: Esc, Tab, Enter,
    // Backspace, Delete, the menu key, and the arrows, Home, End, Page Up/Down alone.
    Q_INVOKABLE bool reserved(const QString& sequence) const;
    // The action that has `sequence`, other than `exceptId`; empty when none.
    Q_INVOKABLE QString owner(const QString& sequence, const QString& exceptId = {}) const;
    // Puts `sequence` at `index` (0 or 1) of `id`, taking it from any other action; past
    // the last key it is added.
    Q_INVOKABLE void assign(const QString& id, int index, const QString& sequence);
    // Removes the key at `index`; a second one moves up.
    Q_INVOKABLE void clear(const QString& id, int index);
    Q_INVOKABLE void reset(const QString& id);
    Q_INVOKABLE void resetAll();
    // As printed on the keys: "Ctrl+←", "Ctrl+Shift+Tab".
    Q_INVOKABLE QString displayText(const QString& sequence) const;

  signals:
    void changed();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct Action {
        QString id;
        QString text;
        Group group;
        QStringList defaults;
    };
    [[nodiscard]] const Action* find(const QString& id) const;
    void store(const QString& id);

    std::vector<Action> m_actions;
    QHash<QString, QStringList> m_keys;
    QSettings m_store;
};

} // namespace cirrus::ui
