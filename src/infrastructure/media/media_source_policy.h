#pragma once
#include "domain/playback/media_source.h"
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <optional>

namespace cirrus::media
{
// What addresses one provider may issue for media. A closed scope per provider, NOT arbitrary
// forwarding: only an initial URL from that provider is admitted, and a redirect must stay in
// the same provider's group.
struct ProviderPolicy {
    QStringList suffixes; // host equals a suffix or is a subdomain of one
    QSet<QString> schemes;
    bool allowPorts = false;
    bool allowUserInfo = false;
    bool allowFragment = false;
};

inline const ProviderPolicy& policyFor(domain::SourceProvider provider)
{
    static const ProviderPolicy netease{
        {QStringLiteral("music.126.net")}, {QStringLiteral("https")}, false, false, false};
    // The Bilibili CDNs only; http is allowed and ports are, as the provider issues them.
    // Not hdslb.com (subtitle trust domain) and not akamaized.net.
    static const ProviderPolicy bilibili{{QStringLiteral("bilivideo.com"),
                                          QStringLiteral("bilivideo.cn"),
                                          QStringLiteral("mountaintoys.cn")},
                                         {QStringLiteral("https"), QStringLiteral("http")},
                                         true,
                                         false,
                                         false};
    return provider == domain::SourceProvider::Bilibili ? bilibili : netease;
}

// The address as it may be fetched for `provider`, or null. `upgradeInitialHttp`: an initial
// provider address given over http may be upgraded to https (Netease's CDN); redirects must
// already be https unless the provider's own policy admits http.
inline std::optional<QUrl> approvedMediaUrl(domain::SourceProvider provider, QUrl url,
                                            bool upgradeInitialHttp = false)
{
    const auto& policy = policyFor(provider);
    const auto host = url.host();
    if (!url.isValid() || url.toEncoded().size() > 8192 || url.isRelative() ||
        (!policy.allowUserInfo && !url.userInfo().isEmpty()) ||
        (!policy.allowFragment && url.hasFragment()) ||
        (!policy.allowPorts && url.port(-1) != -1) || host.size() > 253 ||
        url.path().isEmpty() || url.path() == QStringLiteral("/"))
        return std::nullopt;
    bool hostOk = false;
    for (const auto& suffix : policy.suffixes) {
        if (host == suffix || host.endsWith(QChar(u'.') + suffix)) {
            hostOk = true;
            break;
        }
    }
    if (!hostOk)
        return std::nullopt;
    for (const auto c : host) {
        if (!(c >= u'a' && c <= u'z') && !(c >= u'0' && c <= u'9') && c != u'-' && c != u'.')
            return std::nullopt;
    }
    if (url.scheme() == QStringLiteral("http") && upgradeInitialHttp)
        url.setScheme(QStringLiteral("https"));
    if (!policy.schemes.contains(url.scheme()))
        return std::nullopt;
    return url;
}
} // namespace cirrus::media
