#pragma once

#include "spring_math.h"
#include "tokens.h"

#include <QAbstractAnimation>
#include <QObject>
#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Retargetable spring value. Bind a property to `value` and change `target`:
//   Spring { id: slide; target: open ? 240 : 0; spec: Theme.motion.defaultSpatial }
//   Item { x: slide.value }
// A new target keeps the current velocity, so interrupted motion stays smooth.
// The value starts at the initial target; spatial specs jump when motion is reduced.
class Spring : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(qreal target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(qreal value READ value NOTIFY valueChanged)
    Q_PROPERTY(qreal velocity READ velocity NOTIFY valueChanged)
    Q_PROPERTY(cirrus::ui::SpringSpec spec READ spec WRITE setSpec NOTIFY specChanged)
    Q_PROPERTY(qreal threshold READ threshold WRITE setThreshold NOTIFY thresholdChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

  public:
    explicit Spring(QObject* parent = nullptr);
    ~Spring() override;

    [[nodiscard]] qreal target() const { return m_target; }
    void setTarget(qreal target);
    [[nodiscard]] qreal value() const { return m_value; }
    [[nodiscard]] qreal velocity() const { return m_velocity; }
    [[nodiscard]] SpringSpec spec() const { return m_spec; }
    void setSpec(const SpringSpec& spec);
    [[nodiscard]] qreal threshold() const { return m_threshold; }
    void setThreshold(qreal threshold);
    [[nodiscard]] bool running() const;

    // Jump to `value` and make it the target, without animation.
    Q_INVOKABLE void snapTo(qreal value);

    void classBegin() override {}
    void componentComplete() override;

  signals:
    void targetChanged();
    void valueChanged();
    void specChanged();
    void thresholdChanged();
    void runningChanged();

  private:
    class Driver;
    void tick(int milliseconds);
    void restartSegment();
    void settle();

    Driver* m_driver = nullptr; // made on the first motion
    SpringSpec m_spec = Motion::spec(Motion::DefaultSpatial);
    qreal m_target = 0;
    qreal m_value = 0;
    qreal m_velocity = 0;
    qreal m_threshold = 0.01;
    SpringState m_segmentStart;
    bool m_complete = false;
};

} // namespace cirrus::ui
