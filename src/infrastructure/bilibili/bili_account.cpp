#include "bili_account.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <utility>

namespace cirrus::bilibili
{
using namespace domain;
namespace
{
const QString kCookieKey = QStringLiteral("bilibili.cookie");
}

Account::Account(secrets::SecretStore& store, QObject* parent)
    : QObject(parent), m_store(store), m_http(std::make_unique<HttpClient>())
{
    connect(m_http.get(), &HttpClient::completed, this, &Account::received);
}

Account::Account(secrets::SecretStore& store, std::unique_ptr<QNetworkAccessManager> manager,
                 QObject* parent)
    : QObject(parent), m_store(store), m_http(std::make_unique<HttpClient>(std::move(manager)))
{
    connect(m_http.get(), &HttpClient::completed, this, &Account::received);
}

void Account::publish(BiliAccountStatus status, QString name)
{
    emit changed({status, std::move(name)});
}

void Account::restore()
{
    if (m_busy)
        return;
    const auto revision = ++m_revision;
    m_busy = true;
    publish(BiliAccountStatus::Checking);
    m_store.read(kCookieKey, this, [this, revision](secrets::SecretResult result) {
        if (revision != m_revision)
            return;
        if (m_clearRequested) {
            m_busy = false;
            m_clearRequested = false;
            clear();
            return;
        }
        if (result.error == secrets::SecretError::NotFound) {
            m_busy = false;
            publish(BiliAccountStatus::LoggedOut);
            return;
        }
        if (!result.ok()) {
            m_busy = false;
            publish(BiliAccountStatus::StorageError);
            return;
        }
        m_cookie = std::move(result.data);
        emit cookieChanged();
        check(revision);
    });
}

void Account::save(QByteArray cookie)
{
    if (m_busy)
        return;
    cookie = cookie.trimmed();
    if (cookie.isEmpty() || cookie.size() > 16384 || cookie.contains('\r') || cookie.contains('\n') ||
        cookie.contains('\0') || !cookie.contains("SESSDATA=")) {
        publish(BiliAccountStatus::InvalidCookie);
        return;
    }
    const auto revision = ++m_revision;
    m_busy = true;
    publish(BiliAccountStatus::Checking);
    m_store.write(kCookieKey, cookie, this,
                  [this, revision, cookie](secrets::SecretError error) mutable {
        if (revision != m_revision)
            return;
        if (m_clearRequested) {
            m_busy = false;
            m_clearRequested = false;
            clear();
            return;
        }
        if (error != secrets::SecretError::None) {
            m_busy = false;
            publish(BiliAccountStatus::StorageError);
            return;
        }
        m_cookie = std::move(cookie);
        emit cookieChanged();
        check(revision);
    });
}

void Account::clear()
{
    m_cookie.clear();
    emit cookieChanged();
    if (m_busy && !m_httpId) {
        m_clearRequested = true;
        return;
    }
    if (const auto id = std::exchange(m_httpId, 0))
        m_http->cancel(id);
    const auto revision = ++m_revision;
    m_busy = true;
    publish(BiliAccountStatus::Checking);
    m_store.remove(kCookieKey, this, [this, revision](secrets::SecretError error) {
        if (revision != m_revision)
            return;
        m_busy = false;
        if (error != secrets::SecretError::None && error != secrets::SecretError::NotFound) {
            publish(BiliAccountStatus::StorageError);
            return;
        }
        if (m_clearRequested)
            m_clearRequested = false;
        publish(BiliAccountStatus::LoggedOut);
    });
}

void Account::check(quint64 revision)
{
    if (revision != m_revision)
        return;
    if (m_cookie.isEmpty()) {
        m_busy = false;
        publish(BiliAccountStatus::LoggedOut);
        return;
    }
    m_httpId = m_http->get(QUrl(QStringLiteral("https://api.bilibili.com/x/web-interface/nav")),
                           m_cookie, true);
    if (!m_httpId) {
        m_busy = false;
        publish(BiliAccountStatus::InvalidCookie);
    }
}

void Account::received(quint64 id, HttpOutcome outcome)
{
    if (id != m_httpId)
        return;
    m_httpId = 0;
    m_busy = false;
    if (!outcome.ok) {
        publish(BiliAccountStatus::NetworkError);
        return;
    }
    const auto root = QJsonDocument::fromJson(outcome.body).object();
    const auto data = root.value(QStringLiteral("data")).toObject();
    if (root.value(QStringLiteral("code")).toInt(-1) != 0 ||
        !data.value(QStringLiteral("isLogin")).toBool()) {
        publish(BiliAccountStatus::LoggedOut);
        return;
    }
    publish(BiliAccountStatus::LoggedIn, data.value(QStringLiteral("uname")).toString());
}
} // namespace cirrus::bilibili
