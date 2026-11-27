#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScopedPointer>
#include <QtTest>

class PageStackTest : public QObject
{
    Q_OBJECT
  private:
    static bool settled(QObject* stack)
    {
        return !stack->property("incoming").value<QQuickItem*>()
               && !stack->property("outgoing").value<QQuickItem*>();
    }

  private slots:
    void reversingThenNavigatingReleasesEachRetiredPageOnce()
    {
        QQmlEngine engine;
        engine.addImportPath(QString::fromUtf8(CIRRUS_QML_IMPORT_PATH));
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/Cirrus/Test/PageStack/PageStackHarness.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> harness(component.create());
        QVERIFY2(harness, qPrintable(component.errorString()));

        auto* stack = harness->property("stack").value<QObject*>();
        QVERIFY(stack);
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "start"));
        QTRY_COMPARE_WITH_TIMEOUT(stack->property("depth").toInt(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(settled(stack), 5000);

        QVERIFY(QMetaObject::invokeMethod(harness.data(), "pushPage", Q_ARG(int, 2)));
        bool popped = false;
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "popPage", Q_RETURN_ARG(bool, popped)));
        QVERIFY(popped);
        // Replace a reversing transition before it finishes. The removed incoming page
        // is no longer owned by entries and must be released exactly once.
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "pushPage", Q_ARG(int, 3)));
        QTRY_COMPARE_WITH_TIMEOUT(stack->property("depth").toInt(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(settled(stack), 5000);
        auto* current = stack->property("currentItem").value<QQuickItem*>();
        QVERIFY(current);
        QCOMPARE(current->property("pageId").toInt(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(harness->property("destroyedPages").toInt(), 1, 5000);

        QVERIFY(QMetaObject::invokeMethod(harness.data(), "popPage", Q_RETURN_ARG(bool, popped)));
        QVERIFY(popped);
        QTRY_VERIFY_WITH_TIMEOUT(settled(stack), 5000);
        current = stack->property("currentItem").value<QQuickItem*>();
        QVERIFY(current);
        QCOMPARE(current->property("pageId").toInt(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(harness->property("destroyedPages").toInt(), 2, 5000);
    }
};

QTEST_MAIN(PageStackTest)
#include "page_stack_test.moc"
