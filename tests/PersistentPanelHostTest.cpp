#include <QtTest/QtTest>
#include <QLineEdit>
#include <QPointer>
#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockWidget.h"
#include "DockAreaTitleBar.h"
#include "DockAreaTabBar.h"
#include "DockWidgetTab.h"
#include "DockSplitter.h"
#include "FloatingDockContainer.h"
#include "DockOverlay.h"

using namespace ads;

class PersistentPanelHostTest : public QObject
{
    Q_OBJECT
    int parentChanges = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::ParentChange) ++parentChanges;
        return false;
    }
    static void settle()
    {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
    static void verifyBounds(CDockWidget *dock)
    {
        auto *area = dock->dockAreaWidget();
        QVERIFY(area);
        QCOMPARE(QRect(area->mapFromGlobal(dock->mapToGlobal(QPoint())), dock->size()), area->contentAreaGeometry());
    }
private slots:
    void floatingGroup_redockPreservesInactivePanel_data()
    {
        QTest::addColumn<bool>("persistentHosts");
        QTest::newRow("ordinary-hosts") << false;
        QTest::newRow("persistent-hosts") << true;
    }

    void floatingGroup_redockPreservesInactivePanel()
    {
        using namespace ads;
        QFETCH(bool, persistentHosts);
        struct ConfigRestorer {
            CDockManager::ConfigFlags flags = CDockManager::configFlags();
            ~ConfigRestorer() { CDockManager::setConfigFlags(flags); }
        } restoreConfig;
        CDockManager::setConfigFlag(CDockManager::HalfPanelDropZones, true);
        class TestManager : public CDockManager {
        public:
            using CDockManager::containerOverlay;
            using CDockContainerWidget::dropFloatingWidget;
            using CDockManager::dockAreaOverlay;
        } manager;
        manager.setPersistentDockWidgetHostsEnabled(persistentHosts);
        manager.resize(800, 600);
        manager.show();
        auto makePanel = [&manager](const QString &name) {
            auto *dock = new CDockWidget(&manager, name);
            dock->setWidget(new QLineEdit(name));
            return dock;
        };
        auto *anchor = makePanel(QStringLiteral("Anchor"));
        manager.addDockWidget(CenterDockWidgetArea, anchor);
        QPointer<CDockWidget> graph = makePanel(QStringLiteral("Render Graph"));
        QPointer<CDockWidget> mixer = makePanel(QStringLiteral("Mixer"));
        QPointer<CFloatingDockContainer> floating = manager.addDockWidgetFloating(graph);
        auto *area = graph->dockAreaWidget();
        manager.addDockWidgetTabToArea(mixer, area);
        area->setCurrentDockWidget(mixer);
        floating->resize(500, 400);
        floating->show();
        QCoreApplication::processEvents();
        QCOMPARE(area->currentDockWidget(), mixer.data());

        const QPoint drop = manager.mapToGlobal(QPoint(2, manager.height() / 2));
        manager.containerOverlay()->setAllowedAreas(LeftDockWidgetArea);
        manager.containerOverlay()->showOverlay(&manager, drop);
        manager.dockAreaOverlay()->hideOverlay();
        manager.dropFloatingWidget(floating, drop);
        QCOMPARE(area->dockContainer(), static_cast<CDockContainerWidget *>(&manager));
        if (persistentHosts) {
            QCOMPARE(graph->parentWidget(), static_cast<QWidget *>(&manager));
            QCOMPARE(mixer->parentWidget(), static_cast<QWidget *>(&manager));
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        QVERIFY(!floating);
        QVERIFY(graph);
        QVERIFY(mixer);
        QCOMPARE(area->dockWidgetsCount(), 2);
        QCOMPARE(area->titleBar()->tabBar()->count(), 2);
        QVERIFY(graph->tabWidget()->isVisible());
        QVERIFY(mixer->tabWidget()->isVisible());
        area->setCurrentDockWidget(graph);
        QVERIFY(graph->isVisible());
        area->setCurrentDockWidget(mixer);
        QVERIFY(mixer->isVisible());
        QVERIFY(area->features().testFlag(CDockWidget::DockWidgetMovable));
    }

    void immediateFloatingRestoreKeepsNewContainersRegistered_data()
    {
        QTest::addColumn<bool>("prepared");
        QTest::newRow("ordinary") << false;
        QTest::newRow("prepared") << true;
    }

    void immediateFloatingRestoreKeepsNewContainersRegistered()
    {
        QFETCH(bool, prepared);
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        manager.setPreparedLayoutsEnabled(prepared);
        auto *main = new CDockWidget("Main");
        main->setWidget(new QLineEdit("Main content"));
        manager.addDockWidget(LeftDockWidgetArea, main);
        const auto docked = manager.saveState();
        auto *floating = new CDockWidget("Floating");
        QPointer<QWidget> content = new QLineEdit("Retained floating content");
        floating->setWidget(content);
        manager.addDockWidgetFloating(floating);
        const auto withFloating = manager.saveState();
        for (int i = 0; i < 3; ++i) {
            QVERIFY(manager.restoreState(docked));
            QVERIFY(manager.restoreState(withFloating));
            QCOMPARE(manager.dockContainers().size(), 2);
            QVERIFY(manager.dockContainers().contains(floating->dockContainer()));
            QCOMPARE(floating->widget(), content.data());
        }
        settle();
        QCOMPARE(manager.dockContainers().size(), 2);
        QVERIFY(floating->dockContainer());
        QCOMPARE(floating->widget(), content.data());
    }

    void activationChangesPreserveFloatingWindows()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        auto *main = new CDockWidget("Main");
        main->setWidget(new QLineEdit("Main"));
        manager.addDockWidget(LeftDockWidgetArea, main);
        auto *dock = new CDockWidget("Floating");
        dock->setWidget(new QLineEdit("Floating"));
        manager.resize(1200, 800);
        manager.show();
        QPointer<CFloatingDockContainer> floating = manager.addDockWidgetFloating(dock);
        settle();
        QVERIFY(floating && floating->isVisible());
        const auto flags = floating->windowFlags();
        for (auto type : {QEvent::WindowActivate, QEvent::WindowDeactivate}) {
            QEvent event(type);
            QCoreApplication::sendEvent(manager.window(), &event);
            settle();
            QVERIFY(floating && floating->isVisible());
            QCOMPARE(floating->windowFlags(), flags);
        }
    }

    void restoredFloatingPanelsRemainVisible_data()
    {
        QTest::addColumn<bool>("prepared");
        QTest::newRow("ordinary") << false;
        QTest::newRow("prepared") << true;
    }

    void restoredFloatingPanelsRemainVisible()
    {
        QFETCH(bool, prepared);
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        manager.setPreparedLayoutsEnabled(prepared);
        auto *main = new CDockWidget("Main");
        main->setWidget(new QLineEdit("Main"));
        auto *area = manager.addDockWidget(LeftDockWidgetArea, main);
        auto *dock = new CDockWidget("Floating");
        QPointer<QWidget> content = new QLineEdit("Retained floating content");
        dock->setWidget(content);
        manager.resize(1200, 800);
        manager.show();
        manager.addDockWidgetFloating(dock);
        settle();
        QVERIFY(dock->floatingDockContainer()->isVisible());
        const auto state = manager.saveState();
        manager.addDockWidget(RightDockWidgetArea, dock, area);
        settle();
        QVERIFY(manager.restoreState(state));
        settle();
        QVERIFY(dock->isInFloatingContainer());
        QTRY_VERIFY(dock->floatingDockContainer()->isVisible());
        QTRY_VERIFY(content->isVisible());
        QCOMPARE(dock->widget(), content.data());
        verifyBounds(dock);
    }

    void rootReplacementKeepsReusedSplittersPaintEnabled()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        manager.setPreparedLayoutsEnabled(true);
        auto *a = new CDockWidget("A"), *b = new CDockWidget("B"), *timeline = new CDockWidget("Timeline");
        for (auto *dock : {a, b, timeline}) dock->setWidget(new QLineEdit(dock->windowTitle()));
        auto *left = manager.addDockWidget(LeftDockWidgetArea, a);
        manager.addDockWidget(RightDockWidgetArea, b, left);
        auto *oldRoot = manager.activeRootSplitter();
        manager.resize(1200, 800);
        manager.show();
        settle();
        const QSize viewport = oldRoot->size();
        timeline->setFixedHeight(24);
        QVERIFY(oldRoot->updatesEnabled());
        manager.addDockWidget(BottomDockWidgetArea, timeline);
        QCOMPARE(manager.activeRootSplitter()->size(), viewport);
        QVERIFY(manager.activeRootSplitter() != oldRoot);
        QVERIFY(manager.activeRootSplitter()->isAncestorOf(oldRoot));
        QVERIFY2(oldRoot->updatesEnabled(), "A former root reused inside the active tree must be able to paint");
        manager.resize(1200, 800);
        manager.show();
        settle();
        for (auto *dock : {a, b, timeline}) {
            for (QWidget *widget = dock->dockAreaWidget(); widget && widget != &manager; widget = widget->parentWidget())
                QVERIFY(widget->updatesEnabled());
        }
    }

    void restoreBeforeFirstShowKeepsBodiesAboveDockTree()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        auto *a = new CDockWidget("A"), *b = new CDockWidget("B");
        a->setWidget(new QLineEdit("Visible body A"));
        b->setWidget(new QLineEdit("Visible body B"));
        auto *left = manager.addDockWidget(LeftDockWidgetArea, a);
        manager.addDockWidget(RightDockWidgetArea, b, left);
        const auto original = manager.saveState();
        manager.addDockWidget(CenterDockWidgetArea, b, left);
        QVERIFY(manager.restoreState(original));
        manager.resize(1000,700);
        manager.show();
        settle();
        for (auto *dock : {a,b}) {
            QVERIFY(dock->isVisible());
            verifyBounds(dock);
            auto *hit = manager.childAt(dock->geometry().center());
            QVERIFY2(hit == dock || dock->isAncestorOf(hit),
                     "Live panel body is obscured by the dock tree after initial show");
        }
    }

    void customGroupsKeepSurfaceParentsAndState()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        QList<CDockWidget*> docks;
        QList<QLineEdit*> views;
        for (int i = 0; i < 4; ++i) {
            auto *dock = new CDockWidget(QString::number(i));
            auto *view = new QLineEdit(QStringLiteral("active view %1").arg(i));
            dock->setWidget(view);
            docks << dock; views << view;
        }
        auto *left = manager.addDockWidget(LeftDockWidgetArea, docks[0]);
        manager.addDockWidget(CenterDockWidgetArea, docks[1], left);
        auto *right = manager.addDockWidget(RightDockWidgetArea, docks[2], left);
        manager.addDockWidget(CenterDockWidgetArea, docks[3], right);
        docks[1]->setAsCurrentTab(); docks[2]->setAsCurrentTab();
        const auto first = manager.saveState();
        manager.addDockWidget(CenterDockWidgetArea, docks[2], left);
        manager.addDockWidget(BottomDockWidgetArea, docks[0], right);
        manager.addDockWidget(CenterDockWidgetArea, docks[1], docks[0]->dockAreaWidget());
        docks[0]->setAsCurrentTab(); docks[3]->setAsCurrentTab();
        const auto second = manager.saveState();
        manager.resize(1000, 700); manager.show(); settle();
        docks[0]->winId(); // Exercise a native descendant as well as plain widgets.
        settle();
        parentChanges = 0;
        for (auto *dock : docks) dock->installEventFilter(this);
        for (auto *view : views) view->installEventFilter(this);
        for (int cycle = 0; cycle < 8; ++cycle) {
            const bool a = cycle % 2 == 0;
            QVERIFY(manager.restoreState(a ? first : second)); settle();
            QCOMPARE(parentChanges, 0);
            for (int i = 0; i < docks.size(); ++i) {
                QCOMPARE(docks[i]->parentWidget(), &manager);
                QCOMPARE(docks[i]->widget(), views[i]);
                QCOMPARE(views[i]->text(), QStringLiteral("active view %1").arg(i));
                if (docks[i]->isVisible()) verifyBounds(docks[i]);
            }
            QCOMPARE(docks[0]->dockAreaWidget()->currentDockWidget(), a ? docks[1] : docks[0]);
            QCOMPARE(docks[2]->dockAreaWidget()->currentDockWidget(), docks[2]);
            manager.resize(1000 + cycle * 10, 700 + cycle * 10); settle();
            for (auto *dock : docks) if (dock->isVisible()) verifyBounds(dock);
        }
        for (auto *dock : docks) dock->removeEventFilter(this);
        for (auto *view : views) view->removeEventFilter(this);
    }

    void tabsCloseReopenAndRedockRemainInteractive()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        auto *a = new CDockWidget("A"), *b = new CDockWidget("B");
        a->setWidget(new QLineEdit("A")); b->setWidget(new QLineEdit("B"));
        auto *area = manager.addDockWidget(CenterDockWidgetArea, a);
        manager.addDockWidget(CenterDockWidgetArea, b, area);
        manager.resize(900, 600); manager.show(); settle();
        a->setAsCurrentTab(); settle();
        QVERIFY(a->isVisible()); QVERIFY(!b->isVisible()); verifyBounds(a);
        QTest::mouseClick(area->titleBar()->tabBar()->tab(1), Qt::LeftButton);
        settle();
        QVERIFY(b->isVisible()); QVERIFY(!a->isVisible()); verifyBounds(b);
        auto *input = qobject_cast<QLineEdit*>(b->widget());
        QTest::keyClicks(input, " edited");
        QCOMPARE(input->text(), QStringLiteral("B edited"));
        b->toggleView(false); settle();
        QVERIFY(!b->isVisible()); QVERIFY(a->isVisible());
        b->toggleView(true); settle(); QVERIFY(b->isVisible()); verifyBounds(b);
        manager.addDockWidget(BottomDockWidgetArea, b, a->dockAreaWidget()); settle();
        QVERIFY(a->isVisible()); QVERIFY(b->isVisible());
        verifyBounds(a); verifyBounds(b);
        manager.addDockWidget(CenterDockWidgetArea, b, a->dockAreaWidget()); settle();
        QCOMPARE(a->dockAreaWidget(), b->dockAreaWidget());
        QVERIFY(b->isVisible()); QVERIFY(!a->isVisible()); verifyBounds(b);
        manager.hide(); settle(); QVERIFY(!b->isVisible());
        manager.show(); settle(); QVERIFY(b->isVisible()); verifyBounds(b);
    }

    void preparedTreesActivateWithoutRebuildingAndInvalidateEdits()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        manager.setPreparedLayoutsEnabled(true);
        auto *a = new CDockWidget("A"), *b = new CDockWidget("B"), *c = new CDockWidget("C");
        for (auto *dock : {a,b,c}) dock->setWidget(new QLineEdit(dock->windowTitle()));
        auto *left = manager.addDockWidget(LeftDockWidgetArea, a);
        manager.addDockWidget(CenterDockWidgetArea, b, left);
        manager.addDockWidget(RightDockWidgetArea, c, left);
        a->setAsCurrentTab();
        const auto first = manager.saveState();
        manager.addDockWidget(BottomDockWidgetArea, b, left);
        manager.addDockWidget(CenterDockWidgetArea, c, b->dockAreaWidget());
        c->setAsCurrentTab();
        const auto second = manager.saveState();
        manager.resize(1000,700); manager.show(); settle();
        QList<QPointer<CDockAreaWidget>> firstAreas, secondAreas;
        parentChanges = 0;
        for (auto *dock : {a,b,c}) dock->installEventFilter(this);
        for (int cycle = 0; cycle < 8; ++cycle) {
            const bool firstLayout = cycle % 2 == 0;
            QVERIFY(manager.restoreState(firstLayout ? first : second)); settle();
            QCOMPARE(manager.lastRestoreUsedPreparedLayout(), cycle >= 2);
            auto &areas = firstLayout ? firstAreas : secondAreas;
            if (areas.isEmpty()) {
                for (int i = 0; i < manager.dockAreaCount(); ++i) areas.append(manager.dockArea(i));
            } else {
                QCOMPARE(manager.dockAreaCount(), areas.size());
                for (int i = 0; i < areas.size(); ++i) QCOMPARE(manager.dockArea(i), areas[i].data());
            }
            QCOMPARE(b->dockAreaWidget(), firstLayout ? a->dockAreaWidget() : c->dockAreaWidget());
            QCOMPARE(b->dockAreaWidget()->currentDockWidget(), firstLayout ? a : c);
            for (auto *dock : {a,b,c}) if (dock->isVisible()) verifyBounds(dock);
            QCOMPARE(parentChanges, 0);
        }
        // Editing the active B layout must not overwrite the saved B snapshot.
        manager.addDockWidget(RightDockWidgetArea, c, a->dockAreaWidget()); settle();
        QVERIFY(manager.restoreState(first)); settle();
        QVERIFY(manager.restoreState(second)); settle();
        QVERIFY(!manager.lastRestoreUsedPreparedLayout());
        QCOMPARE(b->dockAreaWidget(), c->dockAreaWidget());
        auto *area = a->dockAreaWidget();
        const auto before = manager.saveState();
        QVERIFY(!manager.restoreState("not a layout")); settle();
        QCOMPARE(a->dockAreaWidget(), area);
        QCOMPARE(manager.saveState(), before);
        for (auto *dock : {a,b,c}) dock->removeEventFilter(this);
        manager.removeDockWidget(b);
        delete b;
        settle();
        QVERIFY(manager.restoreState(first)); settle();
        QVERIFY(!manager.lastRestoreUsedPreparedLayout());
        QVERIFY(a->isVisible()); verifyBounds(a);
    }

    void boundsActivationKeepsChromeLiveAndSupportsImmediateInteraction()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        manager.setPreparedLayoutsEnabled(true);
        auto *a = new CDockWidget("A"), *b = new CDockWidget("B"), *c = new CDockWidget("C");
        for (auto *dock : {a,b,c}) dock->setWidget(new QLineEdit(dock->windowTitle()));
        auto *left = manager.addDockWidget(LeftDockWidgetArea, a);
        manager.addDockWidget(CenterDockWidgetArea, b, left);
        manager.addDockWidget(RightDockWidgetArea, c, left);
        a->setAsCurrentTab();
        manager.resize(1100,750); manager.show(); settle();
        const auto first = manager.saveState();
        manager.addDockWidget(BottomDockWidgetArea, b, c->dockAreaWidget());
        manager.addDockWidget(CenterDockWidgetArea, a, b->dockAreaWidget());
        b->setAsCurrentTab();
        const auto second = manager.saveState();
        QVERIFY(manager.restoreState(first)); settle();
        auto *firstRoot = manager.activeRootSplitter();
        QVERIFY(manager.restoreState(second)); settle();
        auto *secondRoot = manager.activeRootSplitter();
        QVERIFY(firstRoot != secondRoot);
        struct ChromeEvents : QObject {
            int hideShow = 0, reparent = 0, resize = 0;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::Show || event->type() == QEvent::Hide) ++hideShow;
                if (event->type() == QEvent::ParentChange) ++reparent;
                if (event->type() == QEvent::Resize) ++resize;
                return false;
            }
        } events;
        for (auto *root : {firstRoot, secondRoot}) {
            root->installEventFilter(&events);
            for (auto *area : root->findChildren<CDockAreaWidget*>()) area->installEventFilter(&events);
        }
        const auto activeConnection = connect(&manager, &CDockManager::restoringState, &manager, [&] {
            QVERIFY(manager.updatesEnabled());
        });
        // No event-loop turn is needed between prepared activations. Geometry
        // and hit testing are correct immediately; no hidden subtree is shown.
        for (int cycle = 0; cycle < 8; ++cycle) {
            QVERIFY(manager.activatePreparedState(cycle % 2 ? second : first));
            QVERIFY(manager.updatesEnabled());
            QCOMPARE(manager.activeRootSplitter(), cycle % 2 ? secondRoot : firstRoot);
            QCOMPARE(firstRoot->updatesEnabled(), cycle % 2 == 0);
            QCOMPARE(secondRoot->updatesEnabled(), cycle % 2 != 0);
            for (auto *dock : {a,b,c}) {
                QCOMPARE(dock->parentWidget(), &manager);
                if (dock->isVisible()) {
                    verifyBounds(dock);
                    QVERIFY(manager.rect().contains(dock->geometry()));
                    auto *hit = manager.childAt(dock->geometry().center());
                    QVERIFY(hit == dock || dock->isAncestorOf(hit));
                }
            }
        }
        QCOMPARE(events.hideShow, 0);
        QCOMPARE(events.reparent, 0);
        QCOMPARE(events.resize, 0);
        disconnect(activeConnection);
        // Retained tabs are live controls, with only the active chrome onscreen.
        a->setAsCurrentTab(); settle();
        QVERIFY(a->isVisible()); QVERIFY(!b->isVisible()); verifyBounds(a);
        auto *edit = qobject_cast<QLineEdit*>(a->widget());
        QTest::keyClicks(edit, " retained");
        QVERIFY(edit->text().endsWith(" retained"));
        manager.resize(1300,850); settle(); verifyBounds(a);
        // A changed outgoing selection does not make a prepared destination cold.
        // Its edited chrome is discarded without rebuilding the target.
        QVERIFY(manager.activatePreparedState(first));
        QVERIFY(manager.restoreState(first)); settle(); verifyBounds(a);
        manager.addDockWidget(RightDockWidgetArea, b, c->dockAreaWidget()); settle();
        QCOMPARE(b->dockContainer(), &manager); QVERIFY(b->isVisible()); verifyBounds(b);
    }

    void offscreenPreparationDoesNotTouchLivePanelsAndFirstActivationUsesBounds()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        manager.setPreparedLayoutsEnabled(true);
        auto *a = new CDockWidget("A"), *b = new CDockWidget("B"), *c = new CDockWidget("C");
        for (auto *dock : {a,b,c}) dock->setWidget(new QLineEdit(dock->windowTitle()));
        auto *left = manager.addDockWidget(LeftDockWidgetArea, a);
        manager.addDockWidget(CenterDockWidgetArea, b, left);
        manager.addDockWidget(RightDockWidgetArea, c, left);
        a->setAsCurrentTab();
        manager.resize(1000,750); manager.show(); settle();
        const auto first = manager.saveState();
        manager.addDockWidget(BottomDockWidgetArea, b, c->dockAreaWidget());
        manager.addDockWidget(CenterDockWidgetArea, a, b->dockAreaWidget());
        b->setAsCurrentTab();
        c->toggleView(false);
        const auto second = manager.saveState();
        QVERIFY(manager.restoreState(first)); settle();
        const auto before = manager.saveState();
        auto *root = manager.activeRootSplitter();
        auto *area = a->dockAreaWidget();
        auto *tab = a->tabWidget();
        const auto bounds = a->geometry();
        const auto visible = a->isVisible();
        QSignalSpy restored(&manager, &CDockManager::stateRestored);
        parentChanges = 0;
        for (auto *dock : {a,b,c}) dock->installEventFilter(this);
        QVERIFY(manager.prepareState(second));
        QVERIFY(manager.isStatePrepared(second));
        auto *preparedRoot = manager.preparedRootForState(second);
        QVERIFY(preparedRoot);
        QVERIFY(!preparedRoot->updatesEnabled());
        settle();
        QCOMPARE(restored.count(), 0);
        QCOMPARE(manager.activeRootSplitter(), root);
        QCOMPARE(manager.saveState(), before);
        QCOMPARE(a->dockAreaWidget(), area);
        QCOMPARE(a->tabWidget(), tab);
        QCOMPARE(a->geometry(), bounds);
        QCOMPARE(a->isVisible(), visible);
        QCOMPARE(parentChanges, 0);
        QVERIFY(!manager.prepareState("invalid"));
        QCOMPARE(manager.saveState(), before);
        QVERIFY(manager.restoreState(second));
        QVERIFY(manager.lastRestoreUsedBoundsActivation());
        QCOMPARE(manager.activeRootSplitter(), preparedRoot);
        QVERIFY(preparedRoot->updatesEnabled());
        QVERIFY(!root->updatesEnabled());
        QCOMPARE(a->dockAreaWidget(), b->dockAreaWidget());
        QCOMPARE(b->dockAreaWidget()->currentDockWidget(), b);
        QVERIFY(b->isVisible()); QVERIFY(!a->isVisible()); QVERIFY(c->isClosed());
        verifyBounds(b);
        QCOMPARE(parentChanges, 0);
        for (auto *dock : {a,b,c}) dock->removeEventFilter(this);
    }

    void restoreHidesReusedGroupWhosePanelsCloseOrFloat()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        auto *main = new CDockWidget("Main"), *side = new CDockWidget("Side"), *moving = new CDockWidget("Moving");
        main->setWidget(new QLineEdit("Main")); side->setWidget(new QLineEdit("Side"));
        moving->setWidget(new QLineEdit("Moving"));
        auto *mainArea = manager.addDockWidget(LeftDockWidgetArea, main);
        auto *sideArea = manager.addDockWidget(RightDockWidgetArea, side, mainArea);
        manager.resize(1200, 800); manager.show(); settle();
        side->toggleView(false);
        manager.addDockWidgetFloating(moving); settle();
        const auto state = manager.saveState();
        side->toggleView(true);
        manager.addDockWidget(CenterDockWidgetArea, moving, sideArea); settle();
        sideArea->setCurrentDockWidget(moving); settle();
        QVERIFY(sideArea->isVisible());
        QVERIFY(manager.restoreState(state)); settle();
        QVERIFY(side->isClosed());
        QVERIFY(moving->isInFloatingContainer());
        for (auto *area : manager.findChildren<CDockAreaWidget*>())
            QVERIFY(!area->isVisible() || area->openDockWidgetsCount() > 0);
    }

    void floatingAndReturningChangesHostOnce()
    {
        CDockManager manager;
        manager.setPersistentDockWidgetHostsEnabled(true);
        auto *a = new CDockWidget("A"), *b = new CDockWidget("B");
        a->setWidget(new QLineEdit("A")); b->setWidget(new QLineEdit("B"));
        auto *area = manager.addDockWidget(CenterDockWidgetArea, a);
        manager.addDockWidget(RightDockWidgetArea, b, area);
        manager.resize(900, 600); manager.show(); settle();
        auto *floating = manager.addDockWidgetFloating(b); settle();
        QVERIFY(floating); QVERIFY(b->isFloating());
        QCOMPARE(b->parentWidget(), b->dockContainer());
        QVERIFY(b->isVisible()); verifyBounds(b);
        manager.addDockWidget(CenterDockWidgetArea, b, a->dockAreaWidget()); settle();
        QVERIFY(!b->isFloating()); QCOMPARE(b->parentWidget(), &manager);
        QVERIFY(b->isVisible()); verifyBounds(b);
    }
};
QTEST_MAIN(PersistentPanelHostTest)
#include "PersistentPanelHostTest.moc"
