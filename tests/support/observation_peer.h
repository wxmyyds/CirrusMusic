#pragma once

#include "infrastructure/ipc/playback_observation_codec.h"
#include <QJsonDocument>
#include <QTest>
#include <array>
#include <sys/socket.h>
#include <unistd.h>

namespace cirrus::test
{
// Consumer of the real framed socket protocol; no lyric projection or encoding here.
class ObservationPeer final
{
  public:
    explicit ObservationPeer(int descriptor) : m_fd(descriptor) {}
    ~ObservationPeer()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    ObservationPeer(const ObservationPeer&) = delete;
    ObservationPeer& operator=(const ObservationPeer&) = delete;

    QJsonObject request(const QString& method, QJsonObject extra = {})
    {
        const auto id = QString::number(++m_id);
        QJsonObject message{{"v", 1}, {"kind", "request"}, {"id", id}, {"method", method}};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            message[it.key()] = it.value();
        const auto frame = infrastructure::ipc::PlaybackObservationCodec::frame(
            QJsonDocument(message).toJson(QJsonDocument::Compact));
        if (::send(m_fd, frame.constData(), size_t(frame.size()), MSG_NOSIGNAL | MSG_DONTWAIT) !=
            frame.size())
            return {};
        if (!QTest::qWaitFor([this] { return completeFrame(); }, 2000))
            return {};
        const auto result = QJsonDocument::fromJson(m_bytes.mid(4, m_length)).object();
        m_bytes.remove(0, 4 + m_length);
        return result["id"].toString() == id ? result : QJsonObject{};
    }
    bool closed() const
    {
        char byte;
        return ::recv(m_fd, &byte, 1, MSG_DONTWAIT | MSG_PEEK) == 0;
    }

  private:
    bool completeFrame()
    {
        std::array<char, 4096> buffer{};
        for (int i = 0; i < 8; ++i) {
            const auto n = ::recv(m_fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
            if (n <= 0)
                break;
            m_bytes.append(buffer.data(), n);
        }
        if (m_bytes.size() < 4)
            return false;
        m_length = 0;
        for (int i = 0; i < 4; ++i)
            m_length = (m_length << 8) | quint8(m_bytes[i]);
        return m_length <= quint32(infrastructure::ipc::PlaybackObservationCodec::FrameBytes) &&
               m_bytes.size() >= 4 + m_length;
    }
    int m_fd;
    quint64 m_id = 0;
    QByteArray m_bytes;
    quint32 m_length = 0;
};
} // namespace cirrus::test
