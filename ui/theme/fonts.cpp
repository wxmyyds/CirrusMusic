#include "fonts.h"
#include "logging/log.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QResource>
#include <QUrl>

#include <algorithm>

namespace cirrus::ui
{
namespace
{
const QString kSymbolsPath =
    QStringLiteral(":/qt/qml/Cirrus/Theme/fonts/MaterialSymbolsRounded.ttf");

QString registerFont(const QString& path)
{
    const QResource resource(path);
    const QByteArray data =
        resource.compressionAlgorithm() == QResource::NoCompression
            ? QByteArray::fromRawData(reinterpret_cast<const char*>(resource.data()),
                                      resource.size())
            : resource.uncompressedData();
    const int id = data.isEmpty() ? -1 : QFontDatabase::addApplicationFontFromData(data);
    const QStringList families =
        id < 0 ? QStringList{} : QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        log::warn(lcUiTheme, "font.register_failed", {"font", path});
        return {};
    }
    return families.constFirst();
}
} // namespace

Fonts* Fonts::instance()
{
    static Fonts* fonts = [] {
        auto* created = new Fonts(QCoreApplication::instance());
        return created;
    }();
    return fonts;
}

Fonts* Fonts::create(QQmlEngine*, QJSEngine*)
{
    Fonts* fonts = instance();
    QJSEngine::setObjectOwnership(fonts, QJSEngine::CppOwnership);
    return fonts;
}

Fonts::Fonts(QObject* parent)
    : QObject(parent), m_symbols(registerFont(kSymbolsPath)),
      m_system(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family())
{
    update();
}

void Fonts::setSource(TextSource source)
{
    if (m_source == source)
        return;
    m_source = source;
    update();
}

void Fonts::setCustomFamily(const QString& family)
{
    if (m_customFamily == family)
        return;
    m_customFamily = family;
    update();
}

void Fonts::update()
{
    QString family = m_system;
    if (m_source == Custom && QFontDatabase::hasFamily(m_customFamily))
        family = m_customFamily;
    if (family == m_sans)
        return;
    m_sans = family;

    // Text that sets no font of its own follows too.
    QFont base = QGuiApplication::font();
    const QFont styled = font(QFont::Normal, base.pixelSize() > 0 ? base.pixelSize() : 14);
    base.setFamilies(styled.families());
    base.clearVariableAxes();
    for (const QFont::Tag& tag : styled.variableAxisTags())
        base.setVariableAxis(tag, styled.variableAxisValue(tag));
    QGuiApplication::setFont(base);
    emit sansChanged();
}

QFont Fonts::font(qreal weight, qreal pixelSize) const { return fontOf(m_sans, weight, pixelSize); }

QFont Fonts::fontOf(const QString& family, qreal weight, qreal pixelSize) const
{
    const bool usable = !family.isEmpty() && (family == m_sans || QFontDatabase::hasFamily(family));
    QFont font(usable ? family : m_sans);
    font.setPixelSize(qRound(pixelSize));
    font.setWeight(QFont::Weight(std::clamp(qRound(weight), 1, 1000)));
    return font;
}

QFont Fonts::tabular(QFont font)
{
    font.setFeature(QFont::Tag("tnum"), 1);
    return font;
}

void Fonts::setFontFiles(const QStringList& files)
{
    if (m_fontFiles == files)
        return;
    m_fontFiles = files;
    for (const QString& file : files)
        loadFontFile(file);
    emit fontFilesChanged();
    // A custom family from one of them may exist now.
    update();
}

QString Fonts::loadFontFile(const QString& file)
{
    const QString path = file.startsWith(QStringLiteral("file:")) ? QUrl(file).toLocalFile() : file;
    if (const auto loaded = m_loaded.constFind(path); loaded != m_loaded.cend())
        return *loaded;
    const int id = QFontDatabase::addApplicationFont(path);
    const QStringList families =
        id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
    const QString family = families.value(0);
    if (!family.isEmpty())
        m_loaded.insert(path, family);
    return family;
}

QStringList Fonts::families() const
{
    QStringList families = QFontDatabase::families();
    families.removeIf([](const QString& family) { return QFontDatabase::isPrivateFamily(family); });
    return families;
}

} // namespace cirrus::ui
