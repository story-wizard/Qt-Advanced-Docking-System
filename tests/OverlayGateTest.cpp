/*******************************************************************************
** [Wizard NLE fork] Unit test for CDockManager drop-overlay gating.
**
** Verifies the public API contract owned by this fork. Live drag/cancel
** behavior is covered by the host Wizard app because it coordinates modifier
** policy and drag timing around this QtADS primitive.
******************************************************************************/

#include <QtTest/QtTest>

#include <QLabel>
#include <QtMath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "DockContainerWidget.h"
#include "DockManager.h"
#include "DockOverlay.h"
#include "DockWidget.h"
#include "FloatingDockContainer.h"
#include "ads_globals.h"

using namespace ads;

namespace
{

CDockWidget* makeDockWidget(CDockManager& Manager, const QString& Title)
{
	auto DockWidget = Manager.createDockWidget(Title);
	DockWidget->setWidget(new QLabel(Title));
	return DockWidget;
}

class ConfigRestorer
{
public:
	ConfigRestorer()
		: Flags(CDockManager::configFlags())
		, EdgeMargin(CDockManager::halfPanelContainerEdgeMargin())
		, StartDragDistanceMultiplier(
			CDockManager::startDragDistanceMultiplier())
		, FloatingWindowDockDistanceMultiplier(
			CDockManager::floatingWindowDockDistanceMultiplier())
	{
	}

	~ConfigRestorer()
	{
		CDockManager::setConfigFlags(Flags);
		CDockManager::setHalfPanelContainerEdgeMargin(EdgeMargin);
		CDockManager::setStartDragDistanceMultiplier(
			StartDragDistanceMultiplier);
		CDockManager::setFloatingWindowDockDistanceMultiplier(
			FloatingWindowDockDistanceMultiplier);
	}

private:
	CDockManager::ConfigFlags Flags;
	int EdgeMargin;
	qreal StartDragDistanceMultiplier;
	qreal FloatingWindowDockDistanceMultiplier;
};

class FloatingDragStartCounter : public QObject
{
public:
	int Count = 0;

protected:
	bool eventFilter(QObject* Watched, QEvent* Event) override
	{
		Q_UNUSED(Watched)
		if (Event->type() == internal::FloatingWidgetDragStartEvent)
		{
			++Count;
		}
		return false;
	}
};

void sendNonClientMouseEvent(QWidget* Target, QEvent::Type Type,
	Qt::MouseButton Button, Qt::MouseButtons Buttons)
{
	const QPoint LocalPos(4, 4);
	const QPoint GlobalPos = Target->mapToGlobal(LocalPos);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	QMouseEvent Event(Type, QPointF(LocalPos), QPointF(GlobalPos), Button,
		Buttons, Qt::NoModifier);
#else
	QMouseEvent Event(Type, LocalPos, GlobalPos, Button, Buttons,
		Qt::NoModifier);
#endif
	QApplication::sendEvent(Target, &Event);
}

#ifdef Q_OS_WIN
class TestFloatingDockContainer : public CFloatingDockContainer
{
public:
	using CFloatingDockContainer::CFloatingDockContainer;
	using CFloatingDockContainer::nativeEvent;
};
#endif

}

class TestDockManager : public CDockManager
{
public:
	using CDockManager::containerOverlay;
	using CDockManager::dockAreaOverlay;
	using CDockContainerWidget::dockAreaHeaderHasDropPriority;
	using CDockContainerWidget::dropFloatingWidget;
	using CDockContainerWidget::showDropOverlays;
};

class OverlayGateTest : public QObject
{
	Q_OBJECT

private slots:
	void startDragDistance_defaultsToTwiceApplicationThreshold();
	void startDragDistance_usesConfiguredMultiplier();
	void floatingWindowDockDistance_defaultsToTwiceApplicationThreshold();
	void floatingWindowDockDistance_usesConfiguredMultiplier();
	void floatingWindowDockDistance_nativeCoordinateScale_data();
	void floatingWindowDockDistance_nativeCoordinateScale();
	void floatingWindowDocking_waitsForActivationDistance();
#ifdef Q_OS_WIN
	void floatingWindowDocking_nativeWindowsMessagesUseFramePixels();
#endif
	void dropOverlaysEnabled_defaultsToTrue();
	void setDropOverlaysEnabled_roundtrips();
	void setDropOverlaysEnabled_equalValueIsNoOp();
	void setDropOverlaysEnabled_falseHidesBothOverlays();
	void outlineOnlyDropPreview_doesNotOwnDragHeader();
	void topHeader_beatsForgivingContainerEdgeOnPreviewAndDrop();
	void topHeader_doesNotBeatExplicitContainerIndicator();
	void dragCancelEvent_isRegisteredAndDistinct();
};

void OverlayGateTest::startDragDistance_defaultsToTwiceApplicationThreshold()
{
	QCOMPARE(CDockManager::startDragDistanceMultiplier(), qreal(2.0));
	QCOMPARE(CDockManager::startDragDistance(),
		QApplication::startDragDistance() * 2);
}

void OverlayGateTest::startDragDistance_usesConfiguredMultiplier()
{
	CDockManager::setStartDragDistanceMultiplier(1.25);
	QCOMPARE(CDockManager::startDragDistanceMultiplier(), qreal(1.25));
	QCOMPARE(CDockManager::startDragDistance(),
		qRound(QApplication::startDragDistance() * 1.25));

	CDockManager::setStartDragDistanceMultiplier(2.0);
}

void OverlayGateTest::floatingWindowDockDistance_defaultsToTwiceApplicationThreshold()
{
	QCOMPARE(CDockManager::floatingWindowDockDistanceMultiplier(), qreal(2.0));
	QCOMPARE(CDockManager::floatingWindowDockDistance(),
		QApplication::startDragDistance() * 2);
}

void OverlayGateTest::floatingWindowDockDistance_usesConfiguredMultiplier()
{
	ConfigRestorer RestoreConfig;
	CDockManager::setFloatingWindowDockDistanceMultiplier(1.25);
	QCOMPARE(CDockManager::floatingWindowDockDistanceMultiplier(), qreal(1.25));
	QCOMPARE(CDockManager::floatingWindowDockDistance(),
		qRound(QApplication::startDragDistance() * 1.25));

	CDockManager::setFloatingWindowDockDistanceMultiplier(0.0);
	QCOMPARE(CDockManager::floatingWindowDockDistanceMultiplier(), qreal(1.25));
}

void OverlayGateTest::floatingWindowDockDistance_nativeCoordinateScale_data()
{
	QTest::addColumn<qreal>("coordinateScale");
	QTest::newRow("100-percent") << qreal(1.0);
	QTest::newRow("125-percent") << qreal(1.25);
	QTest::newRow("150-percent") << qreal(1.5);
	QTest::newRow("200-percent") << qreal(2.0);
	QTest::newRow("300-percent") << qreal(3.0);
}

void OverlayGateTest::floatingWindowDockDistance_nativeCoordinateScale()
{
	QFETCH(qreal, coordinateScale);
	const int LogicalDistance = 40;
	const int NativeDistance = qRound(LogicalDistance * coordinateScale);
	// Non-zero/negative native origins must not contribute to the drag distance.
	const QPoint StartPosition(qRound(1200 * coordinateScale),
		qRound(-400 * coordinateScale));
	auto Reached = [&](const QPoint& Delta)
	{
		return internal::floatingWindowDockDistanceReached(StartPosition,
			StartPosition + Delta, LogicalDistance, coordinateScale);
	};
	QVERIFY(!Reached(QPoint()));
	QVERIFY(!Reached(QPoint(NativeDistance - 1, 0)));
	QVERIFY(Reached(QPoint(NativeDistance, 0)));
	QVERIFY(!Reached(QPoint(0, -NativeDistance + 1)));
	QVERIFY(Reached(QPoint(0, -NativeDistance)));
	// Radial activation, rather than Manhattan distance, also scales correctly.
	QVERIFY(!Reached(QPoint(qRound(24 * coordinateScale),
		qRound(32 * coordinateScale) - 1)));
	QVERIFY(Reached(QPoint(qRound(24 * coordinateScale),
		qRound(32 * coordinateScale))));
}

void OverlayGateTest::floatingWindowDocking_waitsForActivationDistance()
{
#ifndef Q_OS_MACOS
	QSKIP("native floating-window threshold integration is macOS-specific");
#else
	ConfigRestorer RestoreConfig;
	CDockManager::setFloatingWindowDockDistanceMultiplier(2.0);

	TestDockManager Manager;
	Manager.resize(600, 400);
	Manager.show();
	CFloatingDockContainer Floating(&Manager);
	Floating.resize(320, 220);
	Floating.show();
	QApplication::processEvents();
	Floating.move(500, 300);
	QApplication::processEvents();

	FloatingDragStartCounter Counter;
	Floating.installEventFilter(&Counter);
	const int Threshold = CDockManager::floatingWindowDockDistance();
	const QPoint StartPosition = Floating.pos();

	// Releasing after a small native-window move must not arm docking, and the
	// pending state must be cleared so later programmatic movement is harmless.
	sendNonClientMouseEvent(&Floating,
		QEvent::NonClientAreaMouseButtonPress,
		Qt::LeftButton, Qt::LeftButton);
	Floating.move(StartPosition + QPoint(Threshold - 1, 0));
	QApplication::processEvents();
	QCOMPARE(Counter.Count, 0);
	QVERIFY(!Floating.isDraggingActive());
	sendNonClientMouseEvent(&Floating,
		QEvent::NonClientAreaMouseButtonRelease,
		Qt::LeftButton, Qt::NoButton);
	Floating.move(StartPosition + QPoint(Threshold + 8, 0));
	QApplication::processEvents();
	QCOMPARE(Counter.Count, 0);
	QVERIFY(!Floating.isDraggingActive());

	// A new gesture arms at the exact radial threshold and remains active when
	// the window moves back toward its origin.
	const QPoint SecondStartPosition = Floating.pos();
	sendNonClientMouseEvent(&Floating,
		QEvent::NonClientAreaMouseButtonPress,
		Qt::LeftButton, Qt::LeftButton);
	Floating.move(SecondStartPosition + QPoint(Threshold, 0));
	QApplication::processEvents();
	QCOMPARE(Counter.Count, 1);
	QVERIFY(Floating.isDraggingActive());
	Floating.move(SecondStartPosition + QPoint(1, 0));
	QApplication::processEvents();
	QCOMPARE(Counter.Count, 1);
	QVERIFY(Floating.isDraggingActive());
	sendNonClientMouseEvent(&Floating,
		QEvent::NonClientAreaMouseButtonRelease,
		Qt::LeftButton, Qt::NoButton);
	QVERIFY(!Floating.isDraggingActive());
#endif
}

#ifdef Q_OS_WIN
void OverlayGateTest::floatingWindowDocking_nativeWindowsMessagesUseFramePixels()
{
	// Run with QT_SCALE_FACTOR=2 as well to catch native/logical origin mixing.
	if (QGuiApplication::platformName() != QStringLiteral("windows"))
	{
		QSKIP("native HWND integration requires the Windows platform plugin");
	}
	ConfigRestorer RestoreConfig;
	CDockManager::setFloatingWindowDockDistanceMultiplier(3.0);
	CDockManager Manager;
	TestFloatingDockContainer Floating(&Manager);
	Floating.resize(320, 220);
	Floating.move(500, 300);
	Floating.show();
	QApplication::processEvents();
	const HWND Window = reinterpret_cast<HWND>(Floating.winId());
	RECT FrameRect;
	QVERIFY(GetWindowRect(Window, &FrameRect));
	const int NativeDistance = qCeil(
		CDockManager::floatingWindowDockDistance() * Floating.devicePixelRatioF());
	FloatingDragStartCounter Counter;
	Floating.installEventFilter(&Counter);
	auto DispatchNativeMessage = [&](UINT Type, WPARAM WParam, LPARAM LParam)
	{
		MSG Message = {};
		Message.hwnd = Window;
		Message.message = Type;
		Message.wParam = WParam;
		Message.lParam = LParam;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
		long Result = 0;
#else
		qintptr Result = 0;
#endif
		Floating.nativeEvent(QByteArrayLiteral("windows_generic_MSG"),
			&Message, &Result);
		QApplication::processEvents();
	};
	DispatchNativeMessage(WM_NCLBUTTONDOWN, HTCAPTION, 0);
	RECT MovedRect = FrameRect;
	OffsetRect(&MovedRect, NativeDistance - 1, 0);
	DispatchNativeMessage(WM_MOVING, 0, reinterpret_cast<LPARAM>(&MovedRect));
	QCOMPARE(Counter.Count, 0);
	QVERIFY(!Floating.isDraggingActive());
	OffsetRect(&MovedRect, 1, 0);
	DispatchNativeMessage(WM_MOVING, 0, reinterpret_cast<LPARAM>(&MovedRect));
	QCOMPARE(Counter.Count, 1);
	QVERIFY(Floating.isDraggingActive());
	MovedRect = FrameRect;
	DispatchNativeMessage(WM_MOVING, 0, reinterpret_cast<LPARAM>(&MovedRect));
	QVERIFY(Floating.isDraggingActive());
	DispatchNativeMessage(WM_EXITSIZEMOVE, 0, 0);
	QVERIFY(!Floating.isDraggingActive());
}
#endif

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

void OverlayGateTest::outlineOnlyDropPreview_doesNotOwnDragHeader()
{
	TestDockManager Manager;
	Manager.resize(600, 400);
	auto Target = makeDockWidget(Manager, QStringLiteral("Target"));
	auto TargetArea = Manager.addDockWidget(CenterDockWidgetArea, Target);
	Manager.show();
	QApplication::processEvents();

	auto Overlay = Manager.dockAreaOverlay();
	Overlay->setAllowedAreas(CenterDockWidgetArea);
	Overlay->setDropPreviewOutlineOnly(true);
	const QPoint HeaderPos = TargetArea->titleBar()->mapToGlobal(
		TargetArea->titleBar()->rect().center());
	QCOMPARE(Overlay->showOverlay(TargetArea, HeaderPos),
		CenterDockWidgetArea);

	QPixmap TabPreview(120, 24);
	TabPreview.fill(Qt::blue);
	QVERIFY(!Overlay->setDragPreviewHeader(TabPreview, HeaderPos));

	Overlay->setDropPreviewOutlineOnly(false);
	QVERIFY(Overlay->setDragPreviewHeader(TabPreview, HeaderPos));
}

void OverlayGateTest::topHeader_beatsForgivingContainerEdgeOnPreviewAndDrop()
{
	ConfigRestorer RestoreConfig;
	CDockManager::setConfigFlag(CDockManager::HalfPanelDropZones, true);
	CDockManager::setHalfPanelContainerEdgeMargin(24);

	TestDockManager Manager;
	Manager.resize(900, 500);
	auto Left = makeDockWidget(Manager, QStringLiteral("Left"));
	auto Target = makeDockWidget(Manager, QStringLiteral("Target"));
	auto TargetSecond = makeDockWidget(Manager,
		QStringLiteral("Target second tab"));
	auto Source = makeDockWidget(Manager, QStringLiteral("Floating source"));
	auto LeftArea = Manager.addDockWidget(CenterDockWidgetArea, Left);
	auto TargetArea = Manager.addDockWidget(RightDockWidgetArea, Target,
		LeftArea);
	Manager.addDockWidgetTabToArea(TargetSecond, TargetArea);
	Manager.addDockWidget(BottomDockWidgetArea, Source, LeftArea);
	Manager.show();
	QApplication::processEvents();

	auto Floating = new CFloatingDockContainer(Source);
	Floating->show();
	QApplication::processEvents();
	QCOMPARE(Manager.visibleDockAreaCount(), 2);

	const QPoint HeaderPos = TargetArea->titleBar()->mapToGlobal(
		TargetArea->titleBar()->rect().center());
	QVERIFY(TargetArea->titleBarGeometry().contains(
		TargetArea->mapFromGlobal(HeaderPos)));
	QVERIFY(Manager.mapFromGlobal(HeaderPos).y()
		< CDockManager::halfPanelContainerEdgeMargin());

	Manager.containerOverlay()->setAllowedAreas(OuterDockAreas);
	QCOMPARE(Manager.containerOverlay()->showOverlay(&Manager, HeaderPos),
		TopDockWidgetArea);
	QCOMPARE(Manager.containerOverlay()->dropIndicatorAreaUnderCursor(
		HeaderPos), InvalidDockWidgetArea);

	TestDockManager::showDropOverlays(&Manager, &Manager, HeaderPos, false);
	QCOMPARE(Manager.dockAreaOverlay()->visibleDropAreaUnderCursor(HeaderPos),
		CenterDockWidgetArea);
	QVERIFY(Manager.dockAreaOverlay()->dropPreviewOutlineOnly());
	QCOMPARE(Manager.containerOverlay()->visibleDropAreaUnderCursor(HeaderPos),
		InvalidDockWidgetArea);

	const int TargetTabCount = TargetArea->dockWidgets().size();
	Manager.dropFloatingWidget(Floating, HeaderPos);
	QApplication::processEvents();

	QCOMPARE(Source->dockAreaWidget(), TargetArea);
	QCOMPARE(TargetArea->dockWidgets().size(), TargetTabCount + 1);
	QCOMPARE(Manager.visibleDockAreaCount(), 2);
}

void OverlayGateTest::topHeader_doesNotBeatExplicitContainerIndicator()
{
	TestDockManager Manager;
	Manager.resize(600, 400);
	auto Target = makeDockWidget(Manager, QStringLiteral("Target"));
	auto TargetArea = Manager.addDockWidget(CenterDockWidgetArea, Target);
	Manager.show();
	QApplication::processEvents();

	const QPoint HeaderPos = TargetArea->titleBar()->mapToGlobal(
		TargetArea->titleBar()->rect().center());
	QVERIFY(TestDockManager::dockAreaHeaderHasDropPriority(TargetArea,
		HeaderPos, CenterDockWidgetArea, TopDockWidgetArea,
		InvalidDockWidgetArea));
	QVERIFY(!TestDockManager::dockAreaHeaderHasDropPriority(TargetArea,
		HeaderPos, CenterDockWidgetArea, TopDockWidgetArea,
		TopDockWidgetArea));
	// AutoHide sidebar hit zones are intentional targets, not split-edge
	// fallbacks, even when the cursor is not over an indicator glyph.
	for (auto Area : {LeftAutoHideArea, RightAutoHideArea,
		TopAutoHideArea, BottomAutoHideArea})
	{
		QVERIFY(!TestDockManager::dockAreaHeaderHasDropPriority(TargetArea,
			HeaderPos, CenterDockWidgetArea, Area, InvalidDockWidgetArea));
	}
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
