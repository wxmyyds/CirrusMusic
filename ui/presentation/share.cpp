#include "share.h"

#include "app.h"
#include "app_settings.h"
#include "logging/log.h"
#include "notice.h"
#include "share_text.h"

namespace cirrus::ui
{

Share::Share(AppSettings* settings, QObject* parent) : QObject(parent), m_settings(settings) {}

QString Share::defaultTemplate() const { return sharing::defaultTemplate(); }

QString Share::text(const QString& templ, int kind, const QString& key, const QString& title,
                    const QString& author) const
{
    if (kind < Song || kind > User)
        return {};
    const auto k = sharing::Kind(kind);
    const QString link = sharing::link(k, key);
    if (link.isEmpty())
        return {};
    return sharing::expand(templ, {link, title, author, sharing::typeName(k)});
}

bool Share::copy(int kind, const QString& key, const QString& title, const QString& author)
{
    const QString templ = m_settings ? m_settings->shareTemplate() : sharing::defaultTemplate();
    const QString shared = text(templ, kind, key, title, author);
    log::info(lcUi, "share.copy", {{"kind", kind}, {"copied", !shared.isEmpty()}});
    if (shared.isEmpty())
        return false;
    App::copyText(shared);
    emit notice(noticed("share.copied", tr("已复制链接")));
    return true;
}

} // namespace cirrus::ui
