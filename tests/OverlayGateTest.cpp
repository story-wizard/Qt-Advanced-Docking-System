/*******************************************************************************
** [Wizard NLE fork] Unit test for CDockManager drop-overlay gating.
**
** Verifies the public API contract owned by this fork. Live drag/cancel
** behavior is covered by the host Wizard app because it coordinates modifier
** policy and drag timing around this QtADS primitive.
******************************************************************************/

#include <QtTest/QtTest>

#include "DockManager.h"
#include "DockOverlay.h"
#include "ads_globals.h"

using ads::CDockManager;

class TestDockManager : public CDockManager
{
public:
	using CDockManager::containerOverlay;
	using CDockManager::dockAreaOverlay;
};

class OverlayGateTest : public QObject
{
	Q_OBJECT

private slots:
	void dropOverlaysEnabled_defaultsToTrue();
	void setDropOverlaysEnabled_roundtrips();
	void setDropOverlaysEnabled_equalValueIsNoOp();
	void setDropOverlaysEnabled_falseHidesBothOverlays();
	void dragCancelEvent_isRegisteredAndDistinct();
};

void OverlayGateTest::dropOverlaysEnabled_defaultsToTrue()
{
	TestDockManager manager;
	QVERIFY(manager.dropOverlaysEnabled());
}

void OverlayGateTest::setDropOverlaysEnabled_roundtrips()
{
	TestDockManager manager;

	manager.setDropOverlaysEnabled(false);
	QVERIFY(!manager.dropOverlaysEnabled());

	manager.setDropOverlaysEnabled(true);
	QVERIFY(manager.dropOverlaysEnabled());
}

void OverlayGateTest::setDropOverlaysEnabled_equalValueIsNoOp()
{
	TestDockManager manager;
	manager.containerOverlay()->show();
	manager.dockAreaOverlay()->show();

	manager.setDropOverlaysEnabled(true);

	QVERIFY(!manager.containerOverlay()->isHidden());
	QVERIFY(!manager.dockAreaOverlay()->isHidden());
}

void OverlayGateTest::setDropOverlaysEnabled_falseHidesBothOverlays()
{
	TestDockManager manager;
	manager.containerOverlay()->show();
	manager.dockAreaOverlay()->show();
	QVERIFY(!manager.containerOverlay()->isHidden());
	QVERIFY(!manager.dockAreaOverlay()->isHidden());

	manager.setDropOverlaysEnabled(false);

	QVERIFY(manager.containerOverlay()->isHidden());
	QVERIFY(manager.dockAreaOverlay()->isHidden());
}

void OverlayGateTest::dragCancelEvent_isRegisteredAndDistinct()
{
	QVERIFY(ads::internal::FloatingWidgetDragCancelEvent > 0);
	QVERIFY(ads::internal::FloatingWidgetDragCancelEvent
		!= ads::internal::FloatingWidgetDragStartEvent);
	QVERIFY(ads::internal::FloatingWidgetDragCancelEvent
		!= ads::internal::DockedWidgetDragStartEvent);
}

QTEST_MAIN(OverlayGateTest)
#include "OverlayGateTest.moc"
