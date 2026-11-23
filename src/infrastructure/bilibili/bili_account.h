#pragma once
#include "bili_http_client.h"
#include "domain/account/bili_account_types.h"
#include "infrastructure/secrets/secret_store.h"

#include <memory>

namespace cirrus::bilibili
{
class Account final : public QObject
{
    Q_OBJECT
  public:
    explicit Account(secrets::SecretStore& store, QObject* parent = nullptr);
    Account(secrets::SecretStore& store, std::unique_ptr<QNetworkAccessManager> manager,
            QObject* parent = nullptr);
    void restore();
    void save(QByteArray cookie);
    void clear();
    void close();
    [[nodiscard]] QByteArray cookie() const { return m_cookie; }

  signals:
    void changed(cirrus::domain::BiliAccountState state);
    void cookieChanged();

  private:
    void check(quint64 revision);
    void received(quint64 id, HttpOutcome outcome);
    void publish(domain::BiliAccountStatus status, QString name = {});
    secrets::SecretStore& m_store;
    std::unique_ptr<HttpClient> m_http;
    QByteArray m_cookie;
    quint64 m_revision = 0;
    quint64 m_httpId = 0;
    bool m_busy = false;
    bool m_clearRequested = false;
    bool m_closing = false;
};
} // namespace cirrus::bilibili
