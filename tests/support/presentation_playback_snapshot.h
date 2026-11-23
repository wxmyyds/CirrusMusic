#pragma once

#include "sources.h"

namespace cirrus::test
{

class UiPlaybackSnapshot final : public ui::PlaybackSource
{
  public:
    ui::PlaybackSnapshot state;
    std::function<void()> changed;

    void publish(ui::PlaybackSnapshot snapshot)
    {
        state = std::move(snapshot);
        if (changed)
            changed();
    }
    const ui::PlaybackSnapshot& snapshot() const override { return state; }
    void setChangedHandler(std::function<void()> handler) override
    {
        changed = std::move(handler);
    }
    void playSongs(const QStringList&, int, const ui::PlayOrigin&) override {}
    void playVoices(const QStringList&, int) override {}
    bool canPlayVoices() const override { return false; }
    void playPause() override {}
    void next() override {}
    void previous() override {}
    void seek(qint64) override {}
    void playAt(int) override {}
    void removeAt(int) override {}
    void move(int, int) override {}
    bool addSongs(const QStringList&, bool, const ui::PlayOrigin&) override { return false; }
    void clearQueue() override {}
    void setVolume(double) override {}
    void setOrder(ui::playback::Order) override {}
    void setRepeat(ui::playback::Repeat) override {}
    void startFm() override {}
};

} // namespace cirrus::test
