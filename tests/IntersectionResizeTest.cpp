#include <QtTest>
#include <QSplitterHandle>
#include "DockSplitter.h"
#include "DockManager.h"
#include "DockAreaWidget.h"
#include "DockWidget.h"

using ads::CDockSplitter;
class IntersectionResizeTest : public QObject
{
    Q_OBJECT
    static QWidget* panel()
    {
        auto* w = new QWidget;
        w->setMinimumSize(80, 70);
        return w;
    }
    static void move(QSplitterHandle* handle, QPoint global, Qt::MouseButtons buttons)
    {
        QMouseEvent event(QEvent::MouseMove, handle->mapFromGlobal(global), global,
                          Qt::NoButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(handle, &event);
    }
private slots:
    void junction_data()
    {
        QTest::addColumn<bool>("cross");
        QTest::addColumn<bool>("rtl");
        QTest::addColumn<bool>("childOrigin");
        for (bool cross : {false, true})
            for (bool rtl : {false, true})
                for (bool child : {false, true}) {
                    const auto name = QString("%1-%2-%3").arg(cross ? "cross" : "T")
                        .arg(rtl ? "rtl" : "ltr").arg(child ? "branch" : "trunk");
                    QTest::newRow(qPrintable(name)) << cross << rtl << child;
                }
    }
    void junction()
    {
        QFETCH(bool, cross);
        QFETCH(bool, rtl);
        QFETCH(bool, childOrigin);
        CDockSplitter root(Qt::Horizontal);
        root.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
        root.setChildrenCollapsible(false);
        auto* left = new CDockSplitter(Qt::Vertical);
        left->setChildrenCollapsible(false);
        left->addWidget(panel()); left->addWidget(panel());
        root.addWidget(left);
        CDockSplitter* right = nullptr;
        if (cross) {
            right = new CDockSplitter(Qt::Vertical);
            right->setChildrenCollapsible(false);
            right->addWidget(panel()); right->addWidget(panel());
            root.addWidget(right);
        } else root.addWidget(panel());
        root.resize(640, 480);
        root.show();
        root.setSizes({320, 320});
        QTest::qWait(10);
        auto* handle = root.handle(1);
        QPoint start(handle->mapToGlobal(handle->rect().center()).x(),
                           left->handle(1)->mapToGlobal(left->handle(1)->rect().center()).y());
        auto* source = childOrigin ? left->handle(1) : handle;
        if (childOrigin) {
            const QRect bounds(source->mapToGlobal(QPoint()), source->size());
            start.setX(qBound(bounds.left(), start.x(), bounds.right()));
        }
        move(source, start, Qt::NoButton);
        QCOMPARE(source->cursor().shape(), Qt::SizeAllCursor);
        const int oldX = handle->mapToGlobal(QPoint()).x();
        const int oldY = left->handle(1)->mapToGlobal(QPoint()).y();
        QSignalSpy horizontal(&root, &QSplitter::splitterMoved);
        QSignalSpy vertical(left, &QSplitter::splitterMoved);
        const auto highlight = handle->palette().color(QPalette::Active, QPalette::Highlight).rgba();
        auto centerColor = [](QWidget* widget) {
            const auto pixels = widget->grab().toImage();
            return pixels.pixelColor(pixels.rect().center()).rgba();
        };
        QTRY_COMPARE(centerColor(handle), highlight);
        QTRY_COMPARE(centerColor(left->handle(1)), highlight);
        if (right) QTRY_COMPARE(centerColor(right->handle(1)), highlight);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(source, &leave);
        QTRY_VERIFY(centerColor(handle) != highlight);
        QTRY_VERIFY(centerColor(left->handle(1)) != highlight);
        if (right) QTRY_VERIFY(centerColor(right->handle(1)) != highlight);
        move(source, start, Qt::NoButton);
        QTest::mousePress(source, Qt::LeftButton, Qt::NoModifier, source->mapFromGlobal(start));
        QTRY_COMPARE(centerColor(handle), highlight);
        QTRY_COMPARE(centerColor(left->handle(1)), highlight);
        if (right) QTRY_COMPARE(centerColor(right->handle(1)), highlight);
        move(source, start + QPoint(35, 25), Qt::LeftButton);
        QCOMPARE(handle->mapToGlobal(QPoint()).x(), oldX + 35);
        QCOMPARE(left->handle(1)->mapToGlobal(QPoint()).y(), oldY + 25);
        if (right) QCOMPARE(right->sizes(), left->sizes());
        QVERIFY(!horizontal.isEmpty()); QVERIFY(!vertical.isEmpty());
        move(source, start + QPoint(2000, 2000), Qt::LeftButton);
        for (int size : root.sizes()) QVERIFY(size >= 80);
        for (int size : left->sizes()) QVERIFY(size >= 70);
        QTest::mouseRelease(source, Qt::LeftButton);
        QApplication::sendEvent(source, &leave);
        QTRY_VERIFY(centerColor(handle) != highlight);
        QTRY_VERIFY(centerColor(left->handle(1)) != highlight);
        if (right) QTRY_VERIFY(centerColor(right->handle(1)) != highlight);
        // The normal Qt splitter state stores the new sizes without extra state.
        const auto saved = root.saveState();
        const auto sizes = root.sizes();
        root.setSizes({300, 300});
        QVERIFY(root.restoreState(saved));
        QCOMPARE(root.sizes(), sizes);
    }
    void dockLayoutRoundTrip()
    {
        struct Manager : ads::CDockManager { using ads::CDockManager::rootSplitter; };
        Manager manager;
        auto makeDock = [&](const QString& name) {
            auto* dock = manager.createDockWidget(name);
            dock->setWidget(panel());
            return dock;
        };
        auto* a = manager.addDockWidget(ads::LeftDockWidgetArea, makeDock("A"));
        manager.addDockWidget(ads::RightDockWidgetArea, makeDock("B"), a);
        manager.addDockWidget(ads::BottomDockWidgetArea, makeDock("C"), a);
        manager.resize(800, 600); manager.show(); QTest::qWait(10);
        auto* root = manager.rootSplitter();
        auto* branch = root->findChild<CDockSplitter*>();
        QVERIFY(branch);
        auto* h = root->handle(1);
        const QPoint start(h->mapToGlobal(h->rect().center()).x(),
                           branch->handle(1)->mapToGlobal(branch->handle(1)->rect().center()).y());
        QTest::mousePress(h, Qt::LeftButton, Qt::NoModifier, h->mapFromGlobal(start));
        move(h, start + QPoint(30, 20), Qt::LeftButton);
        QTest::mouseRelease(h, Qt::LeftButton);
        const auto rootSizes = root->sizes();
        const auto branchSizes = branch->sizes();
        const auto state = manager.saveState();
        root->setSizes({100, 700}); branch->setSizes({100, 500});
        QVERIFY(manager.restoreState(state));
        QCoreApplication::processEvents();
        QCOMPARE(manager.rootSplitter()->sizes(), rootSizes);
        QCOMPARE(manager.rootSplitter()->findChild<CDockSplitter*>()->sizes(), branchSizes);
    }

    void rotatedJunctionAndParticipantRemoval()
    {
        CDockSplitter root(Qt::Vertical);
        auto* top = new CDockSplitter(Qt::Horizontal);
        top->addWidget(panel()); top->addWidget(panel());
        auto* bottom = new CDockSplitter(Qt::Horizontal);
        bottom->addWidget(panel()); bottom->addWidget(panel());
        root.addWidget(top); root.addWidget(bottom);
        root.resize(640, 480); root.show();
        root.setSizes({240, 240});
        top->setSizes({320, 320}); bottom->setSizes({160, 480});
        QTest::qWait(10);
        auto* h = root.handle(1);
        const QPoint start(top->handle(1)->mapToGlobal(top->handle(1)->rect().center()).x(),
                           h->mapToGlobal(h->rect().center()).y());
        const auto bottomSizes = bottom->sizes();
        const int oldX = top->handle(1)->mapToGlobal(QPoint()).x();
        const int oldY = h->mapToGlobal(QPoint()).y();
        QTest::mousePress(h, Qt::LeftButton, Qt::NoModifier, h->mapFromGlobal(start));
        move(h, start + QPoint(20, 30), Qt::LeftButton);
        QCOMPARE(top->handle(1)->mapToGlobal(QPoint()).x(), oldX + 20);
        QCOMPARE(h->mapToGlobal(QPoint()).y(), oldY + 30);
        QCOMPARE(bottom->sizes(), bottomSizes); // An offset branch is independent.
        delete top->widget(1); // Layout mutations must not leave a dangling handle.
        QCoreApplication::processEvents();
        move(h, start + QPoint(30, 40), Qt::LeftButton);
        QTest::mouseRelease(h, Qt::LeftButton);
    }

    void resizeStartsBeforeHighlightDelay()
    {
        CDockSplitter root(Qt::Horizontal);
        auto* branch = new CDockSplitter(Qt::Vertical);
        branch->addWidget(panel()); branch->addWidget(panel());
        root.addWidget(branch); root.addWidget(panel());
        root.resize(640, 480); root.show(); root.setSizes({320, 320});
        QTest::qWait(10);
        auto* handle = root.handle(1);
        const QPoint start(handle->mapToGlobal(handle->rect().center()).x(),
            branch->handle(1)->mapToGlobal(branch->handle(1)->rect().center()).y());
        const int x = handle->x();
        const int y = branch->handle(1)->y();
        move(handle, start, Qt::NoButton);
        QCOMPARE(handle->cursor().shape(), Qt::SizeAllCursor);
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, handle->mapFromGlobal(start));
        move(handle, start + QPoint(20, 15), Qt::LeftButton);
        QCOMPARE(handle->x(), x + 20);
        QCOMPARE(branch->handle(1)->y(), y + 15);
        const auto pixels = branch->handle(1)->grab().toImage();
        QVERIFY(pixels.pixelColor(pixels.rect().center()).rgba()
            != handle->palette().color(QPalette::Active, QPalette::Highlight).rgba());
        QTest::mouseRelease(handle, Qt::LeftButton);
    }

    void ordinaryBoundary()
    {
        CDockSplitter root(Qt::Horizontal);
        root.addWidget(panel()); root.addWidget(panel());
        root.resize(640, 480); root.show(); QTest::qWait(10);
        auto* handle = root.handle(1);
        const auto start = handle->mapToGlobal(handle->rect().center());
        const auto before = root.sizes();
        move(handle, start, Qt::NoButton);
        QCOMPARE(handle->cursor().shape(), Qt::SplitHCursor);
        QTest::mousePress(handle, Qt::LeftButton);
        move(handle, start + QPoint(30, 0), Qt::LeftButton);
        QTest::mouseRelease(handle, Qt::LeftButton);
        QVERIFY(root.sizes() != before);
    }
};
QTEST_MAIN(IntersectionResizeTest)
#include "IntersectionResizeTest.moc"
