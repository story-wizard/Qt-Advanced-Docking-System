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

class WidgetLifecycleCounter : public QObject
{
public:
	int ParentChanges = 0;
	int Shows = 0;
	int Hides = 0;

	void reset()
	{
		ParentChanges = 0;
		Shows = 0;
		Hides = 0;
	}

protected:
	bool eventFilter(QObject* Watched, QEvent* Event) override
	{
		Q_UNUSED(Watched)
		switch (Event->type())
		{
		case QEvent::ParentChange:
			++ParentChanges;
			break;
		case QEvent::Show:
			++Shows;
			break;
		case QEvent::Hide:
			++Hides;
			break;
		default:
			break;
		}
		return false;
	}
};

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
	void horizontalDrag_usesOverlapHysteresisAndStaysOnTabRail();
};

void TabDragTest::horizontalDrag_usesOverlapHysteresisAndStaysOnTabRail()
{
	CDockManager Manager;
	Manager.resize(720, 450);
	auto First = makeDockWidget(Manager,
		QStringLiteral("Very Wide Dragged Panel Tab"));
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
	QSignalSpy CurrentChangingSpy(DockArea,
		&CDockAreaWidget::currentChanging);
	QSignalSpy CurrentChangedSpy(DockArea,
		&CDockAreaWidget::currentChanged);
	WidgetLifecycleCounter FirstLifecycle;
	First->installEventFilter(&FirstLifecycle);

	const QPoint PressPos = FirstTab->mapToGlobal(FirstTab->rect().center());
	sendMouseEvent(FirstTab, QEvent::MouseButtonPress, PressPos,
		Qt::LeftButton, Qt::LeftButton);
	QApplication::processEvents();
	CurrentChangingSpy.clear();
	CurrentChangedSpy.clear();
	FirstLifecycle.reset();

	const int DragStartLeft = FirstTab->pos().x();
	const int SecondRequiredOverlap = qMax(1,
		(SecondTab->width() + 2) / 3);
	const int SecondBoundary = SecondTab->geometry().left()
		+ SecondRequiredOverlap
		- FirstTab->width();
	const int ThirdRequiredOverlap = qMax(1,
		(ThirdTab->width() + 2) / 3);
	const int ThirdBoundary = ThirdTab->geometry().left()
		+ ThirdRequiredOverlap
		- FirstTab->width();
	auto dragPositionForLeft = [&](int Left)
	{
		return QPoint(PressPos.x() + Left - DragStartLeft, PressPos.y());
	};
	const QPoint BeforeRequiredOverlap = dragPositionForLeft(
		SecondBoundary - 1);
	const QPoint AtRequiredOverlap = dragPositionForLeft(SecondBoundary);
	const QPoint JustPastRequiredOverlap = dragPositionForLeft(
		SecondBoundary + 1);
	const QPoint AtThirdOverlap = dragPositionForLeft(ThirdBoundary);
	const QPoint BackAcrossThird = dragPositionForLeft(ThirdBoundary - 5);
	const QPoint StillBetweenBoundaries = dragPositionForLeft(
		ThirdBoundary - 6);
	const QPoint BackAcrossSecond = dragPositionForLeft(SecondBoundary - 5);
	sendMouseEvent(FirstTab, QEvent::MouseMove, BeforeRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);

	QCOMPARE(FirstTab->dragState(), DraggingTab);
	QCOMPARE(TabMovedSpy.count(), 0);
	QCOMPARE(DockArea->dockWidget(0), First);
	QCOMPARE(CurrentChangingSpy.count(), 0);
	QCOMPARE(CurrentChangedSpy.count(), 0);
	QCOMPARE(FirstLifecycle.ParentChanges, 0);
	QCOMPARE(FirstLifecycle.Shows, 0);
	QCOMPARE(FirstLifecycle.Hides, 0);

	sendMouseEvent(FirstTab, QEvent::MouseMove, AtRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 1);
	QCOMPARE(TabMovedSpy.at(0).at(0).toInt(), 0);
	QCOMPARE(TabMovedSpy.at(0).at(1).toInt(), 1);
	QCOMPARE(DockArea->dockWidget(1), First);
	QCOMPARE(DockArea->currentDockWidget(), First);
	QCOMPARE(CurrentChangingSpy.count(), 0);
	QCOMPARE(CurrentChangedSpy.count(), 0);
	QCOMPARE(FirstLifecycle.ParentChanges, 0);
	QCOMPARE(FirstLifecycle.Shows, 0);
	QCOMPARE(FirstLifecycle.Hides, 0);

	// Small movements around the swap boundary must not immediately undo it.
	sendMouseEvent(FirstTab, QEvent::MouseMove, JustPastRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 1);
	sendMouseEvent(FirstTab, QEvent::MouseMove, AtRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 1);
	QCOMPARE(DockArea->dockWidget(1), First);

	sendMouseEvent(FirstTab, QEvent::MouseMove, AtThirdOverlap,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 2);
	QCOMPARE(TabMovedSpy.at(1).at(0).toInt(), 1);
	QCOMPARE(TabMovedSpy.at(1).at(1).toInt(), 2);
	QCOMPARE(DockArea->dockWidget(2), First);
	QCOMPARE(DockArea->currentDockWidget(), First);
	QCOMPARE(CurrentChangingSpy.count(), 0);
	QCOMPARE(CurrentChangedSpy.count(), 0);
	QCOMPARE(FirstLifecycle.ParentChanges, 0);
	QCOMPARE(FirstLifecycle.Shows, 0);
	QCOMPARE(FirstLifecycle.Hides, 0);
	QCOMPARE(FirstTab->dragState(), DraggingTab);

	sendMouseEvent(FirstTab, QEvent::MouseMove, BackAcrossThird,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 3);
	QCOMPARE(TabMovedSpy.at(2).at(0).toInt(), 2);
	QCOMPARE(TabMovedSpy.at(2).at(1).toInt(), 1);
	QCOMPARE(DockArea->dockWidget(1), First);
	QCOMPARE(FirstTab->dragState(), DraggingTab);

	// The next neighbor has its own boundary; another pixel cannot move it too.
	sendMouseEvent(FirstTab, QEvent::MouseMove, StillBetweenBoundaries,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 3);
	QCOMPARE(DockArea->dockWidget(1), First);

	sendMouseEvent(FirstTab, QEvent::MouseMove, BackAcrossSecond,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 4);
	QCOMPARE(TabMovedSpy.at(3).at(0).toInt(), 1);
	QCOMPARE(TabMovedSpy.at(3).at(1).toInt(), 0);
	QCOMPARE(DockArea->dockWidget(0), First);
	QCOMPARE(FirstTab->dragState(), DraggingTab);

	// Releasing between slots must seat the tab back in its layout position.
	sendMouseEvent(FirstTab, QEvent::MouseMove, BeforeRequiredOverlap,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 4);
	sendMouseEvent(FirstTab, QEvent::MouseButtonRelease, BeforeRequiredOverlap,
		Qt::LeftButton, Qt::NoButton);
	QCOMPARE(FirstTab->dragState(), DraggingInactive);
	QCOMPARE(FirstTab->pos(), QPoint(0, 0));
}

QTEST_MAIN(TabDragTest)
#include "TabDragTest.moc"
