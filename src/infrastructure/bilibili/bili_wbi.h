#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>

namespace cirrus::bilibili
{

// WBI request signing (the web endpoints' `w_rid`/`wts`). The 64-entry permutation and the
// md5 step are the provider's fixed rule; the mixin key is derived from the two `wbi_img`
// file names the nav/ticket answer carries.
class WbiSigner
{
  public:
    // offered `img` and `sub` URLs (or bare names). Empty if either has no usable file name.
    static QString mixinKey(const QString& imgUrl, const QString& subUrl);

    // The query string of `url` plus `wts` and `w_rid`, in the provider's order. `mixinKey`
    // is the 32-character key from mixinKey(); an invalid key returns `url` unchanged.
    // Values are filtered of !'()* and percent-encoded as the provider expects.
    static QUrl sign(const QUrl& url, const QString& mixinKey, qint64 unixSeconds);
};

} // namespace cirrus::bilibili
