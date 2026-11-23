#pragma once

#include "together.h"

#include <QObject>

namespace cirrus::ui
{
class BackendHost;

// Listen together from the host's TogetherSession (pages/together.md §7).
class BackendTogether : public QObject, public TogetherSource
{
  public:
    explicit BackendTogether(BackendHost& host, QObject* parent = nullptr);

    [[nodiscard]] const TogetherSnapshot& snapshot() const override { return m_snapshot; }
    void setHandlers(std::function<void()> changed, std::function<void(together::End)> ended,
                     std::function<void()> listSent) override;
    bool create() override;
    bool join(const QString& link) override;
    void leave() override;
    [[nodiscard]] together::Link linkKind(const QString& link) const override;

  private:
    void refresh();
    void restore();
    void persist();

    BackendHost& m_host;
    TogetherSnapshot m_snapshot;
    bool m_restored = false; // the room of the last run resumed, or nothing to resume
    std::function<void()> m_changed, m_listSent;
    std::function<void(together::End)> m_ended;
};

} // namespace cirrus::ui
