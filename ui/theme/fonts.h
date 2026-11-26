#pragma once

#include <QFont>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace cirrus::ui
{

// Text follows the desktop's system font unless the user picks an installed or imported
// family. Icons use the separately subset Material Symbols font.
class Fonts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(TextSource source READ source WRITE setSource NOTIFY sansChanged)
    Q_PROPERTY(QString customFamily READ customFamily WRITE setCustomFamily NOTIFY sansChanged)
    Q_PROPERTY(QString sans READ sans NOTIFY sansChanged)
    Q_PROPERTY(QString symbols READ symbols CONSTANT)
    // Font files the user picked (fonts not installed in the system), loaded for this
    // run: their families join families() and can be the custom family.
    Q_PROPERTY(QStringList fontFiles READ fontFiles WRITE setFontFiles NOTIFY fontFilesChanged)

  public:
    // Preserve the persisted Custom value; zero is intentionally unassigned.
    enum TextSource { System = 1, Custom = 2 };
    Q_ENUM(TextSource)

    [[nodiscard]] static Fonts* instance();
    static Fonts* create(QQmlEngine* qml, QJSEngine* js);

    [[nodiscard]] TextSource source() const { return m_source; }
    void setSource(TextSource source);
    [[nodiscard]] QString customFamily() const { return m_customFamily; }
    void setCustomFamily(const QString& family);
    [[nodiscard]] QString sans() const { return m_sans; }
    [[nodiscard]] QString symbols() const { return m_symbols; }
    [[nodiscard]] QStringList fontFiles() const { return m_fontFiles; }
    void setFontFiles(const QStringList& files);

    // Text font at a CSS weight (100–900, fractional allowed) and pixel size.
    Q_INVOKABLE QFont font(qreal weight, qreal pixelSize) const;
    // The same in another family (the lyrics may have their own); an empty or
    // unavailable family uses the selected text family.
    Q_INVOKABLE QFont fontOf(const QString& family, qreal weight, qreal pixelSize) const;
    Q_INVOKABLE static QFont tabular(QFont font);
    Q_INVOKABLE QStringList families() const;
    // Loads a font file (a local path or file: URL) for this run; returns its first
    // family, or "" when it is not a font. Add it to fontFiles to keep it.
    Q_INVOKABLE QString loadFontFile(const QString& file);

  signals:
    void sansChanged();
    void fontFilesChanged();

  private:
    explicit Fonts(QObject* parent = nullptr);
    void update();

    TextSource m_source = System;
    QString m_customFamily;
    QString m_sans;
    QString m_symbols;
    QString m_system;
    QStringList m_fontFiles;
    // Path → family of the files loaded so far.
    QHash<QString, QString> m_loaded;
};

} // namespace cirrus::ui
