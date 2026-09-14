/*******************************************************************************
** [Wizard NLE fork] Tests for panel tab reordering before detachment.
*******************************************************************************/

#include <QtTest/QtTest>

#include <QLabel>
#include <QMouseEvent>
#include <QSignalSpy>

#include "DockAreaTabBar.h"
#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockWidget.h"
#include "DockWidgetTab.h"

using namespace ads;

namespace
{

void sendMouseEvent(QWidget* Target, QEvent::Type Type,
	const QPoint& GlobalPos, Qt::MouseButton Button, Qt::MouseButtons Buttons)
{
	const QPoint LocalPos = Target->mapFromGlobal(GlobalPos);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	QMouseEvent Event(Type, QPointF(LocalPos), QPointF(GlobalPos), Button,
		Buttons, Qt::NoModifier);
#else
	QMouseEvent Event(Type, LocalPos, GlobalPos, Button, Buttons,
		Qt::NoModifier);
#endif
	QApplication::sendEvent(Target, &Event);
}

CDockWidget* makeDockWidget(CDockManager& Manager, const QString& Title)
{
	auto DockWidget = Manager.createDockWidget(Title);
	DockWidget->setWidget(new QLabel(Title));
	return DockWidget;
}

}

class TabDragTest : public QObject
{
	Q_OBJECT

private slots:
	void horizontalDrag_reordersAtOneThirdOverlapAndStaysOnTabRail();
};

void TabDragTest::horizontalDrag_reordersAtOneThirdOverlapAndStaysOnTabRail()
{
	CDockManager Manager;
	Manager.resize(720, 450);
	auto First = makeDockWidget(Manager, QStringLiteral("First"));
	auto Second = makeDockWidget(Manager, QStringLiteral("Second"));
	auto Third = makeDockWidget(Manager, QStringLiteral("Third"));
	auto DockArea = Manager.addDockWidget(CenterDockWidgetArea, First);
	Manager.addDockWidgetTabToArea(Second, DockArea);
	Manager.addDockWidgetTabToArea(Third, DockArea);
	Manager.show();
	QApplication::processEvents();

	auto FirstTab = First->tabWidget();
	auto SecondTab = Second->tabWidget();
	auto ThirdTab = Third->tabWidget();
	auto TabBar = DockArea->titleBar()->tabBar();
	QVERIFY(FirstTab->isVisible());
	QVERIFY(SecondTab->isVisible());
	QVERIFY(ThirdTab->isVisible());
	QSignalSpy TabMovedSpy(TabBar, &CDockAreaTabBar::tabMoved);

	const QPoint PressPos = FirstTab->mapToGlobal(FirstTab->rect().center());
	sendMouseEvent(FirstTab, QEvent::MouseButtonPress, PressPos,
		Qt::LeftButton, Qt::LeftButton);
	QApplication::processEvents();

	const int DragStartLeft = FirstTab->pos().x();
	const int RequiredOverlap = qMax(1, (SecondTab->width() + 2) / 3);
	const int ReorderLeft = SecondTab->geometry().left() + RequiredOverlap
		- FirstTab->width();
	auto dragPositionForLeft = [&](int Left)
	{
		return QPoint(PressPos.x() + Left - DragStartLeft, PressPos.y());
	};
	const QPoint BeforeRequiredOverlap = dragPositionForLeft(ReorderLeft - 1);
	const QPoint AtRequiredOverlap = dragPositionForLeft(ReorderLeft);
	const QPoint BeyondRight(
		TabBar->mapToGlobal(QPoint(
			TabBar->width() + CDockManager::startDragDistance(), 0)).x(),
		PressPos.y());
	const QPoint BeyondLeft(
		TabBar->mapToGlobal(QPoint(-CDockManager::startDragDistance(), 0)).x(),
		PressPos.y());
	sendMouseEvent(FirstTab, QEvent::MouseMove, BeforeRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);

	QCOMPARE(FirstTab->dragState(), DraggingTab);
	QCOMPARE(TabMovedSpy.count(), 0);
	QCOMPARE(DockArea->dockWidget(0), First);

	sendMouseEvent(FirstTab, QEvent::MouseMove, AtRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 1);
	QCOMPARE(DockArea->dockWidget(1), First);

	sendMouseEvent(FirstTab, QEvent::MouseMove, BeyondRight,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 2);
	QCOMPARE(DockArea->dockWidget(2), First);
	QCOMPARE(FirstTab->dragState(), DraggingTab);

	sendMouseEvent(FirstTab, QEvent::MouseMove, BeyondLeft,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 3);
	QCOMPARE(DockArea->dockWidget(0), First);
	QCOMPARE(FirstTab->dragState(), DraggingTab);

	// Releasing between slots must seat the tab back in its layout position.
	sendMouseEvent(FirstTab, QEvent::MouseMove, BeforeRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 3);
	sendMouseEvent(FirstTab, QEvent::MouseButtonRelease, BeforeRequiredOverlap,
		Qt::LeftButton, Qt::NoButton);
	QCOMPARE(FirstTab->dragState(), DraggingInactive);
	QCOMPARE(FirstTab->pos(), QPoint(0, 0));
}

QTEST_MAIN(TabDragTest)
#include "TabDragTest.moc"
