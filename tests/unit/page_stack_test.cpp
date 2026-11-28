#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubator>
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
               && !stack->property("outgoing").value<QQuickItem*>()
               && !stack->property("creating").toBool();
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

    void asynchronousPushKeepsDisplayedPageUntilLatestDestinationIsReady()
    {
        QQmlIncubationController controller;
        QQmlEngine engine;
        engine.setIncubationController(&controller);
        engine.addImportPath(QString::fromUtf8(CIRRUS_QML_IMPORT_PATH));
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/Cirrus/Test/PageStack/PageStackHarness.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> harness(component.create());
        QVERIFY2(harness, qPrintable(component.errorString()));

        auto* stack = harness->property("stack").value<QObject*>();
        QVERIFY(stack);
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "start"));
        QCOMPARE(stack->property("depth").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "pushPage", Q_ARG(int, 2)));
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "pushPage", Q_ARG(int, 3)));
        QVERIFY(stack->property("creating").toBool());
        auto* current = stack->property("currentItem").value<QQuickItem*>();
        QVERIFY(current);
        QCOMPARE(current->property("pageId").toInt(), 1);

        controller.incubateFor(100);
        QTRY_VERIFY_WITH_TIMEOUT(settled(stack), 5000);
        QCOMPARE(stack->property("depth").toInt(), 3);
        current = stack->property("currentItem").value<QQuickItem*>();
        QVERIFY(current);
        QCOMPARE(current->property("pageId").toInt(), 3);

        bool popped = false;
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "popPage", Q_RETURN_ARG(bool, popped)));
        QVERIFY(popped);
        QTRY_VERIFY_WITH_TIMEOUT(settled(stack), 5000);
        current = stack->property("currentItem").value<QQuickItem*>();
        QVERIFY(current);
        QCOMPARE(current->property("pageId").toInt(), 2);
    }

    void poppingPendingDestinationDiscardsItsLateObject()
    {
        QQmlIncubationController controller;
        QQmlEngine engine;
        engine.setIncubationController(&controller);
        engine.addImportPath(QString::fromUtf8(CIRRUS_QML_IMPORT_PATH));
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/Cirrus/Test/PageStack/PageStackHarness.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> harness(component.create());
        QVERIFY2(harness, qPrintable(component.errorString()));

        auto* stack = harness->property("stack").value<QObject*>();
        QVERIFY(stack);
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "start"));
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "pushPage", Q_ARG(int, 2)));
        QVERIFY(stack->property("creating").toBool());

        bool popped = false;
        QVERIFY(QMetaObject::invokeMethod(harness.data(), "popPage", Q_RETURN_ARG(bool, popped)));
        QVERIFY(popped);
        QCOMPARE(stack->property("depth").toInt(), 1);
        controller.incubateFor(100);
        QTRY_VERIFY_WITH_TIMEOUT(settled(stack), 5000);
        auto* current = stack->property("currentItem").value<QQuickItem*>();
        QVERIFY(current);
        QCOMPARE(current->property("pageId").toInt(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(harness->property("destroyedPages").toInt(), 1, 5000);
    }
};

QTEST_MAIN(PageStackTest)
#include "page_stack_test.moc"
