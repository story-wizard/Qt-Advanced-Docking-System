/*******************************************************************************
** [Wizard NLE fork] Tests for panel tab reordering before detachment.
*******************************************************************************/

#include <QtTest/QtTest>

#include <algorithm>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalSpy>

#include "DockAreaTabBar.h"
#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "FloatingDragPreview.h"
#include "DockManager.h"
#include "DockOverlay.h"
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

bool anyDockOverlayVisible(CDockManager& Manager)
{
	const auto Overlays = Manager.findChildren<CDockOverlay*>();
	return std::any_of(Overlays.cbegin(), Overlays.cend(),
		[](const CDockOverlay* Overlay) { return Overlay->isVisible(); });
}

bool anyOutlineOnlyDockOverlayVisible(CDockManager& Manager)
{
	const auto Overlays = Manager.findChildren<CDockOverlay*>();
	return std::any_of(Overlays.cbegin(), Overlays.cend(),
		[](const CDockOverlay* Overlay)
		{
			return Overlay->isVisible()
				&& Overlay->dropPreviewOutlineOnly();
		});
}

}

class TabDragTest : public QObject
{
	Q_OBJECT

private slots:
	void inactiveTab_clickActivatesOnlyOnRelease();
	void inactiveTab_reordersWithoutActivation();
	void horizontalDrag_usesOverlapHysteresisAndStaysOnTabRail();
	void externalPreview_shiftsTabsWithoutChangingPanelState();
	void externalPreview_wideSlotKeepsDestinationTabVisible();
	void floatingDrag_reenteringSourceHeaderResumesTabReorder();
	void floatingDrag_enteringAnotherHeaderPreviewsUntilRelease();
	void floatingDrag_singleTabSourceUsesLiveTabSnapshot();
};

void TabDragTest::inactiveTab_clickActivatesOnlyOnRelease()
{
	CDockManager Manager;
	Manager.resize(720, 450);
	auto First = makeDockWidget(Manager, QStringLiteral("First"));
	auto Second = makeDockWidget(Manager, QStringLiteral("Second"));
	auto DockArea = Manager.addDockWidget(CenterDockWidgetArea, First);
	Manager.addDockWidgetTabToArea(Second, DockArea);
	DockArea->setCurrentDockWidget(First);
	Manager.show();
	QApplication::processEvents();

	auto SecondTab = Second->tabWidget();
	QSignalSpy ClickedSpy(SecondTab, &CDockWidgetTab::clicked);
	QSignalSpy CurrentChangingSpy(DockArea,
		&CDockAreaWidget::currentChanging);
	QSignalSpy CurrentChangedSpy(DockArea,
		&CDockAreaWidget::currentChanged);
	const QPoint PressPos = SecondTab->mapToGlobal(
		SecondTab->rect().center());

	sendMouseEvent(SecondTab, QEvent::MouseButtonPress, PressPos,
		Qt::LeftButton, Qt::LeftButton);
	QApplication::processEvents();

	QCOMPARE(ClickedSpy.count(), 0);
	QCOMPARE(CurrentChangingSpy.count(), 0);
	QCOMPARE(CurrentChangedSpy.count(), 0);
	QCOMPARE(DockArea->currentDockWidget(), First);
	QVERIFY(!SecondTab->isActiveTab());

	sendMouseEvent(SecondTab, QEvent::MouseButtonRelease, PressPos,
		Qt::LeftButton, Qt::NoButton);
	QApplication::processEvents();

	QCOMPARE(ClickedSpy.count(), 1);
	QCOMPARE(CurrentChangingSpy.count(), 1);
	QCOMPARE(CurrentChangedSpy.count(), 1);
	QCOMPARE(DockArea->currentDockWidget(), Second);
	QVERIFY(SecondTab->isActiveTab());
}


void TabDragTest::inactiveTab_reordersWithoutActivation()
{
	CDockManager Manager;
	Manager.resize(720, 450);
	auto First = makeDockWidget(Manager, QStringLiteral("First"));
	auto Second = makeDockWidget(Manager, QStringLiteral("Second"));
	auto DockArea = Manager.addDockWidget(CenterDockWidgetArea, First);
	Manager.addDockWidgetTabToArea(Second, DockArea);
	DockArea->setCurrentDockWidget(First);
	Manager.show();
	QApplication::processEvents();

	auto FirstTab = First->tabWidget();
	auto SecondTab = Second->tabWidget();
	auto TabBar = DockArea->titleBar()->tabBar();
	QSignalSpy ClickedSpy(SecondTab, &CDockWidgetTab::clicked);
	QSignalSpy TabMovedSpy(TabBar, &CDockAreaTabBar::tabMoved);
	QSignalSpy CurrentChangingSpy(DockArea,
		&CDockAreaWidget::currentChanging);
	QSignalSpy CurrentChangedSpy(DockArea,
		&CDockAreaWidget::currentChanged);

	const QPoint PressPos = SecondTab->mapToGlobal(
		SecondTab->rect().center());
	const int DragStartLeft = SecondTab->pos().x();
	const int RequiredOverlap = qMax(1, (FirstTab->width() + 2) / 3);
	const int ReorderBoundary = FirstTab->geometry().right()
		- RequiredOverlap + 1;
	const QPoint ReorderPos(
		PressPos.x() + ReorderBoundary - DragStartLeft - 1,
		PressPos.y());

	sendMouseEvent(SecondTab, QEvent::MouseButtonPress, PressPos,
		Qt::LeftButton, Qt::LeftButton);
	sendMouseEvent(SecondTab, QEvent::MouseMove, ReorderPos,
		Qt::NoButton, Qt::LeftButton);
	QApplication::processEvents();

	QCOMPARE(SecondTab->dragState(), DraggingTab);
	QCOMPARE(ClickedSpy.count(), 0);
	QCOMPARE(TabMovedSpy.count(), 1);
	QCOMPARE(DockArea->dockWidget(0), Second);
	QCOMPARE(DockArea->dockWidget(1), First);
	QCOMPARE(DockArea->currentDockWidget(), First);
	QCOMPARE(TabBar->currentTab(), FirstTab);
	QCOMPARE(CurrentChangingSpy.count(), 0);
	QCOMPARE(CurrentChangedSpy.count(), 0);

	sendMouseEvent(SecondTab, QEvent::MouseButtonRelease, ReorderPos,
		Qt::LeftButton, Qt::NoButton);
	QApplication::processEvents();

	QCOMPARE(SecondTab->dragState(), DraggingInactive);
	QCOMPARE(ClickedSpy.count(), 0);
	QCOMPARE(DockArea->currentDockWidget(), First);
	QCOMPARE(TabBar->currentTab(), FirstTab);
}

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
	DockArea->setCurrentDockWidget(First);
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


void TabDragTest::externalPreview_shiftsTabsWithoutChangingPanelState()
{
	CDockManager Manager;
	Manager.resize(720, 450);
	auto First = makeDockWidget(Manager, QStringLiteral("First"));
	auto Second = makeDockWidget(Manager, QStringLiteral("Second"));
	auto Third = makeDockWidget(Manager, QStringLiteral("Third"));
	auto DockArea = Manager.addDockWidget(CenterDockWidgetArea, First);
	Manager.addDockWidgetTabToArea(Second, DockArea);
	Manager.addDockWidgetTabToArea(Third, DockArea);
	DockArea->setCurrentDockWidget(Second);
	auto HeaderControl = new QLabel(QStringLiteral("Zoom"));
	DockArea->titleBar()->insertWidget(1, HeaderControl);
	Manager.show();
	QApplication::processEvents();

	auto TabBar = DockArea->titleBar()->tabBar();
	auto FirstTab = First->tabWidget();
	auto SecondTab = Second->tabWidget();
	auto ThirdTab = Third->tabWidget();
	const QPoint FirstPosition = FirstTab->pos();
	const QPoint SecondPosition = SecondTab->pos();
	const QPoint ThirdPosition = ThirdTab->pos();
	const QPoint ControlPosition = HeaderControl->mapToGlobal(QPoint());
	QSignalSpy TabMovedSpy(TabBar, &CDockAreaTabBar::tabMoved);
	QSignalSpy ExternalPreviewSpy(
		TabBar, &CDockAreaTabBar::externalTabDragPreviewChanged);

	const int PreviewWidth = 80;
	const int FirstRequiredOverlap = qMax(1,
		(FirstTab->width() + 2) / 3);
	const int SecondRequiredOverlap = qMax(1,
		(SecondTab->width() + 2) / 3);
	const int ThirdRequiredOverlap = qMax(1,
		(ThirdTab->width() + 2) / 3);
	const int FirstBoundary = FirstTab->mapToGlobal(QPoint()).x()
		+ FirstRequiredOverlap;
	const int SecondBoundary = SecondTab->mapToGlobal(QPoint()).x()
		+ SecondRequiredOverlap;
	const int ThirdBoundary = ThirdTab->mapToGlobal(QPoint()).x()
		+ ThirdRequiredOverlap;
	const int InitialDraggedLeft = FirstTab->mapToGlobal(QPoint()).x()
		- PreviewWidth / 2;
	QCOMPARE(TabBar->previewExternalTabDrag(InitialDraggedLeft,
		PreviewWidth, 0), 0);
	QCOMPARE(FirstTab->pos(), FirstPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(SecondTab->pos(), SecondPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(ThirdTab->pos(), ThirdPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(TabBar->horizontalScrollBar()->value(), 0);
	QCOMPARE(ExternalPreviewSpy.count(), 1);
	QCOMPARE(ExternalPreviewSpy.first().first().toInt(), PreviewWidth);

	QCOMPARE(TabBar->previewExternalTabDrag(
		FirstBoundary, PreviewWidth, 1), 1);
	QCOMPARE(FirstTab->pos(), FirstPosition);
	QCOMPARE(SecondTab->pos(), SecondPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(ThirdTab->pos(), ThirdPosition + QPoint(PreviewWidth, 0));

	QCOMPARE(TabBar->previewExternalTabDrag(
		SecondBoundary, PreviewWidth, 1), 2);
	QCOMPARE(FirstTab->pos(), FirstPosition);
	QCOMPARE(SecondTab->pos(), SecondPosition);
	QCOMPARE(ThirdTab->pos(), ThirdPosition + QPoint(PreviewWidth, 0));

	QCOMPARE(TabBar->previewExternalTabDrag(
		ThirdBoundary, PreviewWidth, 1), 3);
	QCOMPARE(FirstTab->pos(), FirstPosition);
	QCOMPARE(SecondTab->pos(), SecondPosition);
	QCOMPARE(ThirdTab->pos(), ThirdPosition);

	// Reversing direction requires a small retreat, then restores one sibling
	// per crossed boundary rather than moving the remaining tabs as a unit.
	QCOMPARE(TabBar->previewExternalTabDrag(
		ThirdBoundary - 5, PreviewWidth, -1), 2);
	QCOMPARE(ThirdTab->pos(), ThirdPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(TabBar->previewExternalTabDrag(
		SecondBoundary - 5, PreviewWidth, -1), 1);
	QCOMPARE(SecondTab->pos(), SecondPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(ThirdTab->pos(), ThirdPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(TabBar->previewExternalTabDrag(
		FirstBoundary - 5, PreviewWidth, -1), 0);
	QCOMPARE(FirstTab->pos(), FirstPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(SecondTab->pos(), SecondPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(ThirdTab->pos(), ThirdPosition + QPoint(PreviewWidth, 0));
	QCOMPARE(ExternalPreviewSpy.count(), 1);

	QCOMPARE(DockArea->dockWidget(0), First);
	QCOMPARE(DockArea->dockWidget(1), Second);
	QCOMPARE(DockArea->dockWidget(2), Third);
	QCOMPARE(DockArea->currentDockWidget(), Second);
	QCOMPARE(TabMovedSpy.count(), 0);
	QCOMPARE(HeaderControl->mapToGlobal(QPoint()),
		ControlPosition + QPoint(PreviewWidth, 0));

	TabBar->clearExternalTabDragPreview();
	QCOMPARE(FirstTab->pos(), FirstPosition);
	QCOMPARE(SecondTab->pos(), SecondPosition);
	QCOMPARE(ThirdTab->pos(), ThirdPosition);
	QCOMPARE(HeaderControl->mapToGlobal(QPoint()), ControlPosition);
	QCOMPARE(ExternalPreviewSpy.count(), 2);
	QCOMPARE(ExternalPreviewSpy.last().first().toInt(), 0);
}


void TabDragTest::externalPreview_wideSlotKeepsDestinationTabVisible()
{
	CDockManager Manager;
	Manager.resize(520, 360);
	auto Destination = makeDockWidget(Manager,
		QStringLiteral("Destination"));
	auto DestinationSecond = makeDockWidget(Manager,
		QStringLiteral("Destination Two"));
	auto DockArea = Manager.addDockWidget(CenterDockWidgetArea, Destination);
	Manager.addDockWidgetTabToArea(DestinationSecond, DockArea);
	Manager.show();
	QApplication::processEvents();

	auto TabBar = DockArea->titleBar()->tabBar();
	TabBar->setFixedWidth(160);
	QApplication::processEvents();
	auto DestinationTab = Destination->tabWidget();
	const QPoint DestinationPosition = DestinationTab->pos();
	const int DestinationViewportLeft = DestinationTab->mapTo(
		TabBar->viewport(), QPoint()).x();
	const int OriginalScroll = TabBar->horizontalScrollBar()->value();
	const int PreviewWidth = 240;

	const int BeforeDestinationBoundary =
		DestinationTab->mapToGlobal(QPoint()).x() - PreviewWidth / 2;
	QCOMPARE(TabBar->previewExternalTabDrag(
		BeforeDestinationBoundary, PreviewWidth, 0), 0);
	QCOMPARE(DestinationTab->pos(),
		DestinationPosition + QPoint(PreviewWidth, 0));
	QVERIFY(TabBar->horizontalScrollBar()->value() > OriginalScroll);
	const QRect DestinationViewportRect(
		DestinationTab->mapTo(TabBar->viewport(), QPoint()),
		DestinationTab->size());
	QVERIFY(DestinationViewportRect.intersects(TabBar->viewport()->rect()));
	QVERIFY(DestinationViewportRect.left() > DestinationViewportLeft);
	QVERIFY(DestinationViewportRect.intersected(
		TabBar->viewport()->rect()).width() >= 48);

	TabBar->clearExternalTabDragPreview();
	QCOMPARE(DestinationTab->pos(), DestinationPosition);
	QCOMPARE(TabBar->horizontalScrollBar()->value(), OriginalScroll);
}


void TabDragTest::floatingDrag_reenteringSourceHeaderResumesTabReorder()
{
	CDockManager Manager;
	Manager.resize(720, 450);
	auto First = makeDockWidget(Manager, QStringLiteral("Agent Workspace"));
	auto Second = makeDockWidget(Manager, QStringLiteral("Second"));
	auto Third = makeDockWidget(Manager, QStringLiteral("Third"));
	auto DockArea = Manager.addDockWidget(CenterDockWidgetArea, First);
	Manager.addDockWidgetTabToArea(Second, DockArea);
	Manager.addDockWidgetTabToArea(Third, DockArea);
	DockArea->setCurrentDockWidget(First);
	Manager.show();
	QApplication::processEvents();

	auto FirstTab = First->tabWidget();
	auto ThirdTab = Third->tabWidget();
	auto TabBar = DockArea->titleBar()->tabBar();
	QSignalSpy TabMovedSpy(TabBar, &CDockAreaTabBar::tabMoved);
	WidgetLifecycleCounter FirstLifecycle;
	First->installEventFilter(&FirstLifecycle);

	const QPoint PressPos = FirstTab->mapToGlobal(FirstTab->rect().center());
	sendMouseEvent(FirstTab, QEvent::MouseButtonPress, PressPos,
		Qt::LeftButton, Qt::LeftButton);
	QApplication::processEvents();
	FirstLifecycle.reset();

	const QPoint UndockPos = PressPos
		+ QPoint(0, CDockManager::startDragDistance());
	sendMouseEvent(FirstTab, QEvent::MouseMove, UndockPos,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(FirstTab->dragState(), DraggingFloatingWidget);

	const QPoint HeaderPos = ThirdTab->mapToGlobal(ThirdTab->rect().center());
	sendMouseEvent(FirstTab, QEvent::MouseMove, HeaderPos,
		Qt::NoButton, Qt::LeftButton);

	QCOMPARE(FirstTab->dragState(), DraggingTab);
	QCOMPARE(First->dockAreaWidget(), DockArea);
	QCOMPARE(DockArea->currentDockWidget(), First);
	QCOMPARE(TabMovedSpy.count(), 1);
	QCOMPARE(DockArea->dockWidget(1), First);
	QVERIFY(!anyDockOverlayVisible(Manager));
	QCOMPARE(FirstLifecycle.ParentChanges, 0);
	QCOMPARE(FirstLifecycle.Shows, 0);
	QCOMPARE(FirstLifecycle.Hides, 0);

	const QPoint ContinueRight = HeaderPos + QPoint(1, 0);
	sendMouseEvent(FirstTab, QEvent::MouseMove, ContinueRight,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TabMovedSpy.count(), 2);
	QCOMPARE(DockArea->dockWidget(2), First);
	QCOMPARE(FirstLifecycle.ParentChanges, 0);
	QCOMPARE(FirstLifecycle.Shows, 0);
	QCOMPARE(FirstLifecycle.Hides, 0);

	sendMouseEvent(FirstTab, QEvent::MouseButtonRelease, ContinueRight,
		Qt::LeftButton, Qt::NoButton);
	QCOMPARE(FirstTab->dragState(), DraggingInactive);
}


void TabDragTest::floatingDrag_enteringAnotherHeaderPreviewsUntilRelease()
{
	CDockManager Manager;
	Manager.resize(900, 500);
	auto First = makeDockWidget(Manager, QStringLiteral("Agent Workspace"));
	auto SourceSibling = makeDockWidget(Manager, QStringLiteral("Source"));
	auto TargetSibling = makeDockWidget(Manager, QStringLiteral("Target"));
	auto TargetSecond = makeDockWidget(Manager, QStringLiteral("Target Two"));
	auto SourceArea = Manager.addDockWidget(CenterDockWidgetArea, First);
	Manager.addDockWidgetTabToArea(SourceSibling, SourceArea);
	SourceArea->setCurrentDockWidget(SourceSibling);
	auto TargetArea = Manager.addDockWidget(RightDockWidgetArea,
		TargetSibling, SourceArea);
	Manager.addDockWidgetTabToArea(TargetSecond, TargetArea);
	TargetArea->setCurrentDockWidget(TargetSibling);
	Manager.show();
	QApplication::processEvents();

	auto FirstTab = First->tabWidget();
	auto TargetTab = TargetSibling->tabWidget();
	auto TargetSecondTab = TargetSecond->tabWidget();
	auto TargetTabBar = TargetArea->titleBar()->tabBar();
	const QPoint TargetPosition = TargetTab->pos();
	const QPoint TargetSecondPosition = TargetSecondTab->pos();
	const int TargetViewportLeft = TargetTab->mapTo(
		TargetTabBar->viewport(), QPoint()).x();
	QSignalSpy TabMovedSpy(TargetTabBar, &CDockAreaTabBar::tabMoved);
	WidgetLifecycleCounter FirstLifecycle;
	First->installEventFilter(&FirstLifecycle);

	const QPoint PressPos = FirstTab->mapToGlobal(FirstTab->rect().center());
	sendMouseEvent(FirstTab, QEvent::MouseButtonPress, PressPos,
		Qt::LeftButton, Qt::LeftButton);
	QApplication::processEvents();
	FirstLifecycle.reset();
	const QPoint UndockPos = PressPos
		+ QPoint(0, CDockManager::startDragDistance());
	sendMouseEvent(FirstTab, QEvent::MouseMove, UndockPos,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(FirstTab->dragState(), DraggingFloatingWidget);

	const QPoint SourceContentPos = SourceArea->mapToGlobal(
		SourceArea->contentAreaGeometry().center());
	sendMouseEvent(FirstTab, QEvent::MouseMove, SourceContentPos,
		Qt::NoButton, Qt::LeftButton);
	QVERIFY(anyDockOverlayVisible(Manager));
	const int InsertionWidth = FirstTab->width();

	const QPoint TargetHeaderPos = TargetTab->mapToGlobal(
		QPoint(1, TargetTab->rect().center().y()));
	sendMouseEvent(FirstTab, QEvent::MouseMove, TargetHeaderPos,
		Qt::NoButton, Qt::LeftButton);
	QApplication::processEvents();

	// Entering a foreign header is preview-only. Neither panel area changes
	// until release, and the large panel preview collapses to the tab ghost.
	QCOMPARE(FirstTab->dragState(), DraggingFloatingWidget);
	QCOMPARE(First->dockAreaWidget(), SourceArea);
	QCOMPARE(SourceArea->currentDockWidget(), SourceSibling);
	QCOMPARE(TargetArea->currentDockWidget(), TargetSibling);
	QCOMPARE(TargetArea->dockWidgetsCount(), 2);
	auto Preview = Manager.findChild<CFloatingDragPreview*>();
	QVERIFY(Preview);
	QCOMPARE(Preview->height(), FirstTab->height());
	QCOMPARE(Preview->width(), FirstTab->width());
	QCOMPARE(TargetTab->pos(),
		TargetPosition + QPoint(InsertionWidth, 0));
	QCOMPARE(TargetSecondTab->pos(),
		TargetSecondPosition + QPoint(InsertionWidth, 0));
	const QRect TargetViewportRect(
		TargetTab->mapTo(TargetTabBar->viewport(), QPoint()),
		TargetTab->size());
	QVERIFY(TargetViewportRect.intersects(TargetTabBar->viewport()->rect()));
	QVERIFY(TargetViewportRect.left() > TargetViewportLeft);
	QVERIFY(anyDockOverlayVisible(Manager));
	QVERIFY(anyOutlineOnlyDockOverlayVisible(Manager));
	QCOMPARE(TabMovedSpy.count(), 0);
	QCOMPARE(FirstLifecycle.ParentChanges, 0);
	QCOMPARE(FirstLifecycle.Shows, 0);
	QCOMPARE(FirstLifecycle.Hides, 0);

	const QPoint AfterTargetHeaderPos =
		TargetTab->mapToGlobal(TargetTab->rect().center());
	sendMouseEvent(FirstTab, QEvent::MouseMove, AfterTargetHeaderPos,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TargetTab->pos(), TargetPosition);
	QCOMPARE(TargetSecondTab->pos(),
		TargetSecondPosition + QPoint(InsertionWidth, 0));

	const QPoint AfterTargetSecondHeaderPos =
		TargetSecondTab->mapToGlobal(TargetSecondTab->rect().center());
	sendMouseEvent(FirstTab, QEvent::MouseMove, AfterTargetSecondHeaderPos,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TargetTab->pos(), TargetPosition);
	QCOMPARE(TargetSecondTab->pos(), TargetSecondPosition);

	// Crossing back over the second boundary moves only that tab out of the
	// prospective insertion slot.
	const QPoint BetweenTargetHeaders = TargetTab->mapToGlobal(
		QPoint(1, TargetTab->rect().center().y()));
	sendMouseEvent(FirstTab, QEvent::MouseMove, BetweenTargetHeaders,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(TargetTab->pos(), TargetPosition);
	QCOMPARE(TargetSecondTab->pos(),
		TargetSecondPosition + QPoint(InsertionWidth, 0));
	QCOMPARE(TabMovedSpy.count(), 0);

	sendMouseEvent(FirstTab, QEvent::MouseButtonRelease, BetweenTargetHeaders,
		Qt::LeftButton, Qt::NoButton);
	QCOMPARE(FirstTab->dragState(), DraggingInactive);
	QCOMPARE(First->dockAreaWidget(), TargetArea);
	QCOMPARE(TargetArea->currentDockWidget(), First);
	QCOMPARE(TargetArea->dockWidget(0), TargetSibling);
	QCOMPARE(TargetArea->dockWidget(1), First);
	QCOMPARE(TargetArea->dockWidget(2), TargetSecond);
	QVERIFY(TargetSecondTab->pos().x() > TargetSecondPosition.x());
}


void TabDragTest::floatingDrag_singleTabSourceUsesLiveTabSnapshot()
{
	CDockManager Manager;
	Manager.resize(1000, 500);
	auto Source = makeDockWidget(Manager, QStringLiteral("Render Graph"));
	auto Target = makeDockWidget(Manager, QStringLiteral("Target"));
	auto TargetSecond = makeDockWidget(Manager, QStringLiteral("Target Two"));
	auto SourceArea = Manager.addDockWidget(CenterDockWidgetArea, Source);
	auto TargetArea = Manager.addDockWidget(RightDockWidgetArea,
		Target, SourceArea);
	Manager.addDockWidgetTabToArea(TargetSecond, TargetArea);
	TargetArea->setCurrentDockWidget(Target);
	Manager.show();
	QApplication::processEvents();

	auto SourceTab = Source->tabWidget();
	auto TargetTab = Target->tabWidget();
	auto TargetSecondTab = TargetSecond->tabWidget();
	auto TargetTabBar = TargetArea->titleBar()->tabBar();
	QCOMPARE(SourceArea->dockWidgetsCount(), 1);
	const int SourceTitleBarWidth = SourceArea->titleBar()->width();
	const QPoint TargetPosition = TargetTab->pos();
	const QPoint TargetSecondPosition = TargetSecondTab->pos();

	const QPoint PressPos = SourceTab->mapToGlobal(
		SourceTab->rect().center());
	sendMouseEvent(SourceTab, QEvent::MouseButtonPress, PressPos,
		Qt::LeftButton, Qt::LeftButton);

	// Models a Wizard tab exposing an expanded subtab section after mouse-down
	// but before the vertical detach threshold. The drag snapshot must use the
	// live presentation even though mouse-down no longer activates the panel.
	const int ExpandedTabWidth = 260;
	SourceTab->setFixedWidth(ExpandedTabWidth);
	if (SourceTab->parentWidget() && SourceTab->parentWidget()->layout())
	{
		SourceTab->parentWidget()->layout()->activate();
	}
	QApplication::processEvents();
	QCOMPARE(SourceTab->width(), ExpandedTabWidth);
	QVERIFY(SourceTitleBarWidth > ExpandedTabWidth);

	const QPoint UndockPos = PressPos
		+ QPoint(0, CDockManager::startDragDistance());
	sendMouseEvent(SourceTab, QEvent::MouseMove, UndockPos,
		Qt::NoButton, Qt::LeftButton);
	QCOMPARE(SourceTab->dragState(), DraggingFloatingWidget);

	const QPoint TargetHeaderPos = TargetTab->mapToGlobal(
		QPoint(1, TargetTab->rect().center().y()));
	sendMouseEvent(SourceTab, QEvent::MouseMove, TargetHeaderPos,
		Qt::NoButton, Qt::LeftButton);
	QApplication::processEvents();

	auto Preview = Manager.findChild<CFloatingDragPreview*>();
	QVERIFY(Preview);
	QCOMPARE(Preview->width(), ExpandedTabWidth);
	QCOMPARE(Preview->height(), SourceTab->height());
	QVERIFY(Preview->width() < SourceTitleBarWidth);
	QCOMPARE(TargetTabBar->externalTabDragPreviewWidth(),
		ExpandedTabWidth);
	QCOMPARE(TargetTab->pos(), TargetPosition);
	QCOMPARE(TargetSecondTab->pos(),
		TargetSecondPosition + QPoint(ExpandedTabWidth, 0));

	sendMouseEvent(SourceTab, QEvent::MouseButtonRelease, TargetHeaderPos,
		Qt::LeftButton, Qt::NoButton);
}

QTEST_MAIN(TabDragTest)
#include "TabDragTest.moc"
