#include "wheel_filter.h"

#include <QCoreApplication>
#include <QWheelEvent>

#include <algorithm>

namespace cirrus::ui
{

void Wheel::setSpeed(qreal speed)
{
    speed = std::clamp<qreal>(speed, 0.25, 4);
    if (qFuzzyCompare(speed, m_speed))
        return;
    m_speed = speed;
    emit speedChanged();
}

WheelFilter::WheelFilter(QObject* parent) : QObject(parent) {}

void WheelFilter::setTarget(QQuickItem* target)
{
    if (m_target == target)
        return;
    if (m_target)
        m_target->removeEventFilter(this);
    m_target = target;
    if (m_target)
        m_target->installEventFilter(this);
    emit targetChanged();
}

bool WheelFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_target || event->type() != QEvent::Wheel || m_forwarding)
        return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    // A wheel notch has no scroll phase (touchpad gestures begin, update and end).
    if (wheel->phase() != Qt::NoScrollPhase || wheel->angleDelta().isNull())
        return false;
    const QPoint angle = (QPointF(wheel->angleDelta()) * m_scale).toPoint();
    QWheelEvent notch(wheel->position(), wheel->globalPosition(), QPoint(), angle, wheel->buttons(),
                      wheel->modifiers(), Qt::NoScrollPhase, wheel->inverted(), wheel->source(),
                      wheel->pointingDevice());
    notch.setTimestamp(wheel->timestamp());
    notch.setAccepted(false);
    m_forwarding = true;
    QCoreApplication::sendEvent(m_target, &notch);
    m_forwarding = false;
    event->setAccepted(notch.isAccepted());
    return true;
}

} // namespace cirrus::ui
