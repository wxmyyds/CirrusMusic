#pragma once
#include <QtGlobal>
#include <memory>
#include <vector>

struct AVCodecParameters;
struct AVPacket;
struct CirrusEac3;

namespace cirrus::media
{
// Included only by the Fedora application patch, never by the common source tree.
class FedoraEac3Decoder final
{
  public:
    static std::unique_ptr<FedoraEac3Decoder> create(const AVCodecParameters* parameters,
                                                     int timeBaseNum, int timeBaseDen);
    ~FedoraEac3Decoder();
    FedoraEac3Decoder(const FedoraEac3Decoder&) = delete;
    FedoraEac3Decoder& operator=(const FedoraEac3Decoder&) = delete;
    int sampleRate() const;
    int send(const AVPacket* packet);
    int receive();
    void flush();
    bool frameCorrupt() const { return m_frameCorrupt; }
    const std::vector<float>& samples() const { return m_samples; }
    qint64 pts() const { return m_pts; }

  private:
    FedoraEac3Decoder() = default;
    CirrusEac3* m_decoder = nullptr;
    std::vector<float> m_samples;
    qint64 m_pts = 0;
    bool m_frameCorrupt = false;
};
} // namespace cirrus::media
