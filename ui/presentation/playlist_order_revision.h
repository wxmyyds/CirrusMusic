#pragma once

#include <QStringList>
#include <QtGlobal>

namespace cirrus::ui
{

// A save acknowledges one immutable order. A later local edit must remain on the
// page even if the request succeeds for the older order.
class PlaylistOrderRevision
{
  public:
    struct Snapshot {
        quint64 revision = 0;
        QStringList order;
    };

    [[nodiscard]] Snapshot snapshot(const QStringList& order) const
    {
        return {m_revision, order};
    }
    void changed() { ++m_revision; }
    [[nodiscard]] bool isCurrent(const Snapshot& saved, const QStringList& current) const
    {
        return saved.revision == m_revision && saved.order == current;
    }

  private:
    quint64 m_revision = 0;
};

} // namespace cirrus::ui
