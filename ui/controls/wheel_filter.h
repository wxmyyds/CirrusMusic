#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// How far a mouse wheel notch scrolls, for every scroll view (`ScrollBar`'s WheelFilter) and
// the lyrics: 1 is 120 px a notch in a list. The app binds it to its setting.
//   Wheel.speed  // 0.5 … 3
class Wheel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(qreal speed READ speed WRITE setSpeed NOTIFY speedChanged)

  public:
    explicit Wheel(QObject* parent = nullptr) : QObject(parent) {}
    [[nodiscard]] qreal speed() const { return m_speed; }
    void setSpeed(qreal speed);

  signals:
    void speedChanged();

  private:
    qreal m_speed = 1;
};

// Mouse wheel notches for a scroll view (docs/ui/pages/playlist.md §12): the view gets each
// notch without a pixel delta and with its angle `scale`d, so Qt's own wheel scrolling
// (wheelScrollLines × 24 px × scale a notch, eased, overshooting and springing back at the
// ends) applies on every Qt version. Qt 6.12's Wayland plugin gives notches a 15 px pixel
// delta too, which a Flickable takes for touchpad scrolling (15 px a notch). Touchpad
// gestures (with a scroll phase) pass untouched.
//   WheelFilter { target: list; scale: 5 / 3 }
class WheelFilter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem* target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(qreal scale MEMBER m_scale)

  public:
    explicit WheelFilter(QObject* parent = nullptr);

    QQuickItem* target() const { return m_target; }
    void setTarget(QQuickItem* target);

  signals:
    void targetChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QPointer<QQuickItem> m_target;
    qreal m_scale = 1;
    bool m_forwarding = false;
};

} // namespace cirrus::ui
