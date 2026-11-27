#include "spring.h"

#include <cmath>

namespace cirrus::ui
{
namespace
{
bool same(qreal a, qreal b) { return std::abs(a - b) < 1e-9; }
} // namespace

// Open-ended animation clock. Runs on Qt's animation timer, which Qt Quick
// drives from the render loop, so updates follow the display refresh rate.
class Spring::Driver final : public QAbstractAnimation
{
  public:
    explicit Driver(Spring* owner) : QAbstractAnimation(owner), m_owner(owner) {}
    [[nodiscard]] int duration() const override { return -1; }

  protected:
    void updateCurrentTime(int currentTime) override { m_owner->tick(currentTime); }
    void updateState(State newState, State oldState) override
    {
        if ((newState == Running) != (oldState == Running))
            emit m_owner->runningChanged();
    }

  private:
    Spring* m_owner;
};

Spring::Spring(QObject* parent) : QObject(parent) {}

Spring::~Spring()
{
    if (m_driver)
        m_driver->stop();
}

bool Spring::running() const
{
    return m_driver && m_driver->state() == QAbstractAnimation::Running;
}

void Spring::setTarget(qreal target)
{
    if (same(target, m_target))
        return;
    m_target = target;
    emit targetChanged();
    if (!m_complete)
        return;
    if (m_spec.spatial && Motion::reducedMotion()) {
        settle();
        return;
    }
    restartSegment();
}

void Spring::setSpec(const SpringSpec& spec)
{
    if (spec == m_spec)
        return;
    m_spec = spec;
    emit specChanged();
    if (running())
        restartSegment();
}

void Spring::setThreshold(qreal threshold)
{
    threshold = std::max(threshold, 1e-6);
    if (same(threshold, m_threshold))
        return;
    m_threshold = threshold;
    emit thresholdChanged();
}

void Spring::snapTo(qreal value)
{
    m_target = value;
    emit targetChanged();
    settle();
}

void Spring::componentComplete()
{
    m_complete = true;
    settle();
}

void Spring::restartSegment()
{
    // Continue from the current position and velocity towards the new target.
    m_segmentStart = {m_value - m_target, m_velocity};
    // Made on the first motion: thousands of springs sit still (a hover layer per control).
    if (!m_driver)
        m_driver = new Driver(this);
    if (running())
        m_driver->setCurrentTime(0);
    else
        m_driver->start();
}

void Spring::tick(int milliseconds)
{
    const SpringState s =
        springAdvance(m_spec.stiffness, m_spec.dampingRatio, m_segmentStart, milliseconds / 1000.0);
    m_value = m_target + s.displacement;
    m_velocity = s.velocity;
    if (std::abs(s.displacement) < m_threshold && std::abs(s.velocity) < m_threshold * 10.0) {
        settle();
        return;
    }
    emit valueChanged();
}

void Spring::settle()
{
    if (m_driver)
        m_driver->stop();
    const bool changed = !same(m_value, m_target) || m_velocity != 0.0;
    m_value = m_target;
    m_velocity = 0.0;
    m_segmentStart = {};
    if (changed)
        emit valueChanged();
}

} // namespace cirrus::ui
