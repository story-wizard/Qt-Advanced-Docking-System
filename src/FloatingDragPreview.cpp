//============================================================================
/// \file   FloatingDragPreview.cpp
/// \author Uwe Kindler
/// \date   26.11.2019
/// \brief  Implementation of CFloatingDragPreview
//============================================================================

//============================================================================
//                                   INCLUDES
//============================================================================
#include <AutoHideDockContainer.h>
#include "FloatingDragPreview.h"
#include <iostream>

#include <QEvent>
#include <QApplication>
#include <QPainter>
#include <QKeyEvent>
#include <QPointer>

#include "DockWidget.h"
#include "DockWidgetTab.h"
#include "DockAreaWidget.h"
#include "DockAreaTabBar.h"
#include "DockAreaTitleBar.h"
#include "DockManager.h"
#include "DockContainerWidget.h"
#include "DockOverlay.h"
#include "AutoHideDockContainer.h"
#include "ads_globals.h"

namespace ads
{

/**
 * Private data class (pimpl)
 */
struct FloatingDragPreviewPrivate
{
	CFloatingDragPreview *_this;
	QWidget* Content;
	CDockWidget::DockWidgetFeatures ContentFeatures;
	CDockAreaWidget* ContentSourceArea = nullptr;
	QPoint DragStartMousePosition;
	CDockManager* DockManager;
	CDockContainerWidget *DropContainer = nullptr;
	qreal WindowOpacity;
	bool Hidden = false;
	QPixmap ContentPreviewPixmap;
	QPixmap HeaderPreviewPixmap;
	bool HeaderPreviewInOverlay = false;
	bool Canceled = false;
	bool TabDrag = false;
	bool HeaderOnly = false;
	QPointer<CDockAreaWidget> TabReorderArea;
	int TabReorderIndex = -1;
	int TabInsertionWidth = 0;
	int LastDragMouseX = 0;
	bool HasLastDragMouseX = false;
	QSize ContentPreviewSize;

	// Wayland hybrid drag: during the in-window phase, hit-testing and overlays
	// are confined to this container and driven by an explicit, event-supplied
	// global position instead of the unreliable QCursor::pos().
	CDockContainerWidget* SourceContainer = nullptr;
	QPoint LastGlobalPos;
	bool HasLastGlobalPos = false;

	/**
	 * Returns the last event-supplied global position when available (Wayland
	 * hybrid drag), otherwise the live cursor position.
	 */
	QPoint cursorPos() const
	{
		return HasLastGlobalPos ? LastGlobalPos : QCursor::pos();
	}

	CDockAreaWidget* sourceArea() const
	{
		if (auto DockWidget = qobject_cast<CDockWidget*>(Content))
		{
			return DockWidget->dockAreaWidget();
		}
		return qobject_cast<CDockAreaWidget*>(Content);
	}

	void clearTabReorderPreview()
	{
		if (TabReorderArea)
		{
			TabReorderArea->titleBar()->tabBar()
				->clearExternalTabDragPreview();
		}
		TabReorderArea = nullptr;
		TabReorderIndex = -1;
	}

	void setHeaderOnly(bool Value, CDockAreaWidget* TargetArea = nullptr,
		const QPoint& GlobalPos = QPoint())
	{
		if (Value && HeaderPreviewPixmap.isNull())
		{
			Value = false;
		}
		if (HeaderOnly != Value)
		{
			HeaderOnly = Value;
			if (HeaderOnly)
			{
				const qreal PixelRatio = HeaderPreviewPixmap.devicePixelRatio();
				_this->resize(qRound(HeaderPreviewPixmap.width() / PixelRatio),
					qRound(HeaderPreviewPixmap.height() / PixelRatio));
			}
			else if (ContentPreviewSize.isValid())
			{
				_this->resize(ContentPreviewSize);
			}
			_this->update();
		}

		if (!HeaderOnly || !TargetArea)
		{
			return;
		}

		auto TabBar = TargetArea->titleBar()->tabBar();
		const QPoint TargetGlobal(
			GlobalPos.x() - DragStartMousePosition.x(),
			TabBar->mapToGlobal(QPoint()).y());
		if (_this->isWindow() || !_this->parentWidget())
		{
			_this->move(TargetGlobal);
		}
		else
		{
			_this->move(_this->parentWidget()->mapFromGlobal(TargetGlobal));
		}
	}


	/**
	 * Private data constructor
	 */
	FloatingDragPreviewPrivate(CFloatingDragPreview *_public);
	void updateDropOverlays(const QPoint &GlobalPos);
	void updateDragPreviewHeaders(const QPoint& GlobalTopLeft)
	{
		if (HeaderPreviewPixmap.isNull())
		{
			return;
		}
		const bool HeaderInContainer =
			DockManager->containerOverlay()->setDragPreviewHeader(
			HeaderPreviewPixmap, GlobalTopLeft);
		const bool HeaderInDockArea =
			DockManager->dockAreaOverlay()->setDragPreviewHeader(
			HeaderPreviewPixmap, GlobalTopLeft);
		const bool HeaderInOverlay = HeaderInContainer || HeaderInDockArea;
		if (HeaderPreviewInOverlay != HeaderInOverlay)
		{
			HeaderPreviewInOverlay = HeaderInOverlay;
			_this->update();
		}
	}

	void setHidden(bool Value)
	{
		Hidden = Value;
		_this->update();
	}

	/**
	 * Cancel dragging and emit the draggingCanceled event
	 */
	void cancelDragging()
	{
		Canceled = true;
		clearTabReorderPreview();
		Q_EMIT _this->draggingCanceled();
		DockManager->containerOverlay()->hideOverlay();
		DockManager->dockAreaOverlay()->hideOverlay();
		// Post before close(): WA_DeleteOnClose queues destruction after events
		// already posted to this preview, so the host can observe cancellation.
		qApp->postEvent(_this, new QEvent((QEvent::Type)internal::FloatingWidgetDragCancelEvent));
		_this->close();
	}

	/**
	 * Creates the real floating widget in case the mouse is released outside
	 * outside of any drop area
	 */
	void createFloatingWidget();

	/**
	 * Returns true, if the content is floatable
	 */
	bool isContentFloatable() const
	{
		return this->ContentFeatures.testFlag(CDockWidget::DockWidgetFloatable);
	}

	/**
	 * Returns true, if the content is pinnable
	 */
	bool isContentPinnable() const
	{
		return this->ContentFeatures.testFlag(CDockWidget::DockWidgetPinnable);
	}

	/**
	 * Returns the content features
	 */
	CDockWidget::DockWidgetFeatures contentFeatures() const
	{
		CDockWidget* DockWidget = qobject_cast<CDockWidget*>(Content);
		if (DockWidget)
		{
			return DockWidget->features();
		}

		CDockAreaWidget* DockArea = qobject_cast<CDockAreaWidget*>(Content);
		if (DockArea)
		{
			return DockArea->features();
		}

		return CDockWidget::DockWidgetFeatures();
	}
};
// struct LedArrayPanelPrivate


//============================================================================
void FloatingDragPreviewPrivate::updateDropOverlays(const QPoint &GlobalPos)
{
	const int DragDirection = !HasLastDragMouseX ? 0
		: (GlobalPos.x() > LastDragMouseX ? 1
			: (GlobalPos.x() < LastDragMouseX ? -1 : 0));
	LastDragMouseX = GlobalPos.x();
	HasLastDragMouseX = true;

	if (!_this->isVisible() || !DockManager)
	{
		clearTabReorderPreview();
		setHeaderOnly(false);
		return;
	}

	auto ContainerOverlay = DockManager->containerOverlay();
	auto DockAreaOverlay = DockManager->dockAreaOverlay();
	if (!DockManager->dropOverlaysEnabled())
	{
		clearTabReorderPreview();
		setHeaderOnly(false);
		DropContainer = nullptr;
		ContainerOverlay->hideOverlay();
		DockAreaOverlay->hideOverlay();
		if (CDockManager::testConfigFlag(CDockManager::DragPreviewIsDynamic))
		{
			setHidden(false);
		}
		return;
	}

	CDockContainerWidget *TopContainer = nullptr;
	if (SourceContainer)
	{
		// Wayland in-window phase: the source container is the only candidate.
		// This avoids relying on cross-window global coordinates (unreliable on
		// Wayland) and keeps overlays from leaking onto other top-level windows.
		if (SourceContainer->isVisible())
		{
			QPoint MappedPos = SourceContainer->mapFromGlobal(GlobalPos);
			if (SourceContainer->rect().contains(MappedPos))
			{
				TopContainer = SourceContainer;
			}
		}
	}
	else
	{
		auto Containers = DockManager->dockContainers();
		for (auto ContainerWidget : Containers)
		{
			if (!ContainerWidget->isVisible())
			{
				continue;
			}

			QPoint MappedPos = ContainerWidget->mapFromGlobal(GlobalPos);
			if (ContainerWidget->rect().contains(MappedPos))
			{
				if (!TopContainer || ContainerWidget->isInFrontOf(TopContainer))
				{
					TopContainer = ContainerWidget;
				}
			}
		}
	}

	DropContainer = TopContainer;
	if (!TopContainer)
	{
		clearTabReorderPreview();
		setHeaderOnly(false);
		ContainerOverlay->hideOverlay();
		DockAreaOverlay->hideOverlay();
		if (CDockManager::testConfigFlag(CDockManager::DragPreviewIsDynamic))
		{
			setHidden(false);
		}
		return;
	}

	auto DockDropArea = DockAreaOverlay->dropAreaUnderCursor(GlobalPos);
	auto ContainerDropArea = ContainerOverlay->dropAreaUnderCursor(GlobalPos);

	int VisibleDockAreas = TopContainer->visibleDockAreaCount();

	// Include the overlay widget we're dragging as a visible widget
	auto dockAreaWidget = qobject_cast<CDockAreaWidget*>(Content);
	if (dockAreaWidget && dockAreaWidget->isAutoHide())
	{
		VisibleDockAreas++;
	}

	DockWidgetAreas AllowedContainerAreas = (VisibleDockAreas > 1) ? OuterDockAreas : AllDockAreas;
	//ContainerOverlay->enableDropPreview(ContainerDropArea != InvalidDockWidgetArea);
	auto DockArea = TopContainer->dockAreaAt(GlobalPos);
	// If the dock container contains only one single DockArea, then we need
	// to respect the allowed areas - only the center area is relevant here because
	// all other allowed areas are from the container
	if (VisibleDockAreas == 1 && DockArea)
	{
		AllowedContainerAreas.setFlag(CenterDockWidgetArea, DockArea->allowedAreas().testFlag(CenterDockWidgetArea));
	}

	if (isContentPinnable())
	{
		AllowedContainerAreas |= AutoHideDockAreas;
	}
	ContainerOverlay->setAllowedAreas(AllowedContainerAreas);
	ContainerOverlay->enableDropPreview(ContainerDropArea != InvalidDockWidgetArea);

	// A tab entering a dock-area header has left panel-docking mode. Suppress
	// both blue docking overlays and collapse to a lightweight tab-only preview.
	// The source header resumes its real reorder gesture; a foreign header stays
	// preview-only until release.
	if (TabDrag && DockArea && DockArea->isVisible()
	 && !DockArea->titleBar()->isHidden()
	 && DockArea->titleBarGeometry().contains(
		DockArea->mapFromGlobal(GlobalPos)))
	{
		auto SourceArea = sourceArea();

		if (DockArea == SourceArea
		 || DockArea->allowedAreas().testFlag(CenterDockWidgetArea))
		{
			auto TabBar = DockArea->titleBar()->tabBar();
			if (TabReorderArea != DockArea)
			{
				clearTabReorderPreview();
			}
			TabReorderArea = DockArea;
			if (DockArea == SourceArea)
			{
				TabReorderIndex = TabBar->tabInsertIndexAt(
					TabBar->mapFromGlobal(GlobalPos));
			}
			else
			{
				TabReorderIndex = TabBar->previewExternalTabDrag(
					GlobalPos.x() - DragStartMousePosition.x(),
					TabInsertionWidth, DragDirection);
			}
			ContainerOverlay->hideOverlay();
			DockAreaOverlay->hideOverlay();
			setHeaderOnly(true, DockArea, GlobalPos);
			if (CDockManager::testConfigFlag(
				CDockManager::DragPreviewIsDynamic))
			{
				setHidden(false);
			}
			return;
		}
	}
	clearTabReorderPreview();
	setHeaderOnly(false);

	if (DockArea && DockArea->isVisible() && VisibleDockAreas >= 0 && DockArea != ContentSourceArea)
	{
		DockAreaOverlay->enableDropPreview(true);
		DockAreaOverlay->setAllowedAreas( (VisibleDockAreas == 1) ? NoDockWidgetArea : DockArea->allowedAreas());
		DockWidgetArea Area = DockAreaOverlay->showOverlay(DockArea, GlobalPos);

		// A CenterDockWidgetArea for the dockAreaOverlay() indicates that
		// the mouse is in the title bar. If the ContainerArea is valid
		// then we ignore the dock area of the dockAreaOverlay() and disable
		// the drop preview
		if ((Area == CenterDockWidgetArea) && (ContainerDropArea != InvalidDockWidgetArea))
		{
			DockAreaOverlay->enableDropPreview(false);
			ContainerOverlay->enableDropPreview(true);
		}
		else
		{
			ContainerOverlay->enableDropPreview(InvalidDockWidgetArea == Area);
		}
		ContainerOverlay->showOverlay(TopContainer, GlobalPos);
	}
	else
	{
		DockAreaOverlay->hideOverlay();
		// If there is only one single visible dock area in a container, then
		// it does not make sense to show a dock overlay because the dock area
		// would be removed and inserted at the same position. Only auto hide
		// area is allowed
		if (VisibleDockAreas == 1)
		{
			ContainerOverlay->setAllowedAreas(AutoHideDockAreas);
		}
		ContainerOverlay->showOverlay(TopContainer, GlobalPos);


		if (DockArea == ContentSourceArea && InvalidDockWidgetArea == ContainerDropArea)
		{
			DropContainer = nullptr;
		}
	}

	if (CDockManager::testConfigFlag(CDockManager::DragPreviewIsDynamic))
	{
		setHidden(DockDropArea != InvalidDockWidgetArea || ContainerDropArea != InvalidDockWidgetArea);
	}
}


//============================================================================
FloatingDragPreviewPrivate::FloatingDragPreviewPrivate(CFloatingDragPreview *_public) :
	_this(_public)
{

}


//============================================================================
void FloatingDragPreviewPrivate::createFloatingWidget()
{
	CDockWidget* DockWidget = qobject_cast<CDockWidget*>(Content);
	CDockAreaWidget* DockArea = qobject_cast<CDockAreaWidget*>(Content);

	CFloatingDockContainer* FloatingWidget = nullptr;

	if (DockWidget && DockWidget->features().testFlag(CDockWidget::DockWidgetFloatable))
	{
		FloatingWidget = new CFloatingDockContainer(DockWidget);
	}
	else if (DockArea && DockArea->features().testFlag(CDockWidget::DockWidgetFloatable))
	{
		FloatingWidget = new CFloatingDockContainer(DockArea);
	}

	if (FloatingWidget)
	{
		FloatingWidget->setGeometry(_this->geometry());
		FloatingWidget->show();
		if (!CDockManager::testConfigFlag(CDockManager::DragPreviewHasWindowFrame))
		{
			QApplication::processEvents();
			int FrameHeight = FloatingWidget->frameGeometry().height() - FloatingWidget->geometry().height();
			QRect FixedGeometry = _this->geometry();
			FixedGeometry.adjust(0, FrameHeight, 0, 0);
			FloatingWidget->setGeometry(FixedGeometry);
		}
	}
}


//============================================================================
// Wayland: in the frameless config, parent the preview to the source content's
// top-level window so it renders as a translucent child instead of a Qt::Tool
// top-level. Wayland refuses client-driven move() of top-levels in screen
// coordinates; child widgets composite correctly on every backend. We parent to
// Content->window() (the window the dragged widget/area currently lives in) and
// NOT to parent->window(): parent is the dock manager, whose window is always
// the main window, so a drag started in a floating window would otherwise show
// the preview on the main window. Non-Wayland behavior is unchanged.
CFloatingDragPreview::CFloatingDragPreview(QWidget* Content, QWidget* parent) :
	QWidget((internal::isWayland() && Content
	            && !CDockManager::testConfigFlag(CDockManager::DragPreviewHasWindowFrame))
	            ? Content->window()
	            : parent),
	d(new FloatingDragPreviewPrivate(this))
{
	d->Content = Content;
	d->ContentFeatures = d->contentFeatures();
	setAttribute(Qt::WA_DeleteOnClose);
	if (CDockManager::testConfigFlag(CDockManager::DragPreviewHasWindowFrame))
	{
		setWindowFlags(
			Qt::Window | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
		auto Flags = windowFlags();
		Flags |= Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint;
		setWindowFlags(Flags);
#endif
	}
	else if (internal::isWayland())
	{
		// Translucent child of the source window. Transparent for mouse events
		// so a release over a drop indicator falls through to it; positioning
		// is driven by the grabbing tab/title-bar, not by this preview.
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	else
	{
		setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_TranslucentBackground);
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
		auto Flags = windowFlags();
		Flags |= Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint;
		setWindowFlags(Flags);
#endif
	}

	// Create a static image of the widget that should get undocked
	// This is like some kind preview image like it is uses in drag and drop
	// operations
	if (CDockManager::testConfigFlag(CDockManager::DragPreviewShowsContentPixmap))
	{
		d->ContentPreviewPixmap = QPixmap(Content->size());
		Content->render(&d->ContentPreviewPixmap);

		// Keep the dragged panel's identity legible above the translucent body
		// preview and destination overlays. A tab drag carries the source tab;
		// an area drag carries the complete source title row so a tab group does
		// not look like a single-panel payload.
		QWidget* Header = nullptr;
		if (auto DockWidget = qobject_cast<CDockWidget*>(Content))
		{
			Header = DockWidget->tabWidget();
		}
		else if (auto DockArea = qobject_cast<CDockAreaWidget*>(Content))
		{
			Header = DockArea->titleBar();
		}
		if (Header)
		{
			d->HeaderPreviewPixmap = Header->grab();
		}
	}

	connect(qApp, SIGNAL(applicationStateChanged(Qt::ApplicationState)),
		SLOT(onApplicationStateChanged(Qt::ApplicationState)));

	// The only safe way to receive escape key presses is to install an event
	// filter for the application object
	qApp->installEventFilter(this);
}


//============================================================================
CFloatingDragPreview::CFloatingDragPreview(CDockWidget* Content)
	: CFloatingDragPreview((QWidget*)Content, Content->dockManager())
{
	d->DockManager = Content->dockManager();
	if (Content->dockAreaWidget()->openDockWidgetsCount() == 1)
	{
		d->ContentSourceArea = Content->dockAreaWidget();
	}
	setWindowTitle(Content->windowTitle());
}


//============================================================================
CFloatingDragPreview::CFloatingDragPreview(CDockAreaWidget* Content)
	: CFloatingDragPreview((QWidget*)Content, Content->dockManager())
{
	d->DockManager = Content->dockManager();
	d->ContentSourceArea = Content;
	setWindowTitle(Content->currentDockWidget()->windowTitle());
}


//============================================================================
CFloatingDragPreview::~CFloatingDragPreview()
{
	d->clearTabReorderPreview();
	delete d;
}


//============================================================================
void CFloatingDragPreview::moveFloating()
{
	moveFloating(QCursor::pos());
}


//============================================================================
void CFloatingDragPreview::moveFloating(const QPoint& GlobalPos)
{
	// Only stash the event-supplied position on Wayland, where QCursor::pos()
	// is unreliable and finishDragging() must reuse it. On other platforms
	// finishDragging() keeps using the live QCursor::pos() (unchanged behavior).
	if (internal::isWayland())
	{
		d->LastGlobalPos = GlobalPos;
		d->HasLastGlobalPos = true;
	}
	const int BorderSize = (frameSize().width() - size().width()) / 2;
	const QPoint TargetGlobal = GlobalPos - d->DragStartMousePosition
	    - QPoint(BorderSize, 0);
	// When parented as a child (Wayland frameless), translate the screen-coord
	// target into parent-local space so move() places us under the cursor.
	if (isWindow() || !parentWidget())
	{
		move(TargetGlobal);
	}
	else
	{
		move(parentWidget()->mapFromGlobal(TargetGlobal));
	}
	d->updateDropOverlays(GlobalPos);
	d->updateDragPreviewHeaders(TargetGlobal);
}


//============================================================================
void CFloatingDragPreview::setSourceContainer(CDockContainerWidget* Container)
{
	d->SourceContainer = Container;
}


//============================================================================
void CFloatingDragPreview::cancelDraggingSilently()
{
	// Tear down without performing a drop and without emitting draggingCanceled.
	d->Canceled = true;
	d->clearTabReorderPreview();
	d->DockManager->containerOverlay()->hideOverlay();
	d->DockManager->dockAreaOverlay()->hideOverlay();
	close();
}

//============================================================================
bool CFloatingDragPreview::finishDraggingToSourceTabBar()
{
	auto TargetArea = d->TabReorderArea;
	if (!TargetArea || TargetArea != d->sourceArea())
	{
		return false;
	}

	cancelDraggingSilently();
	return true;
}

//============================================================================
void CFloatingDragPreview::refreshDropOverlays()
{
	// updateDropOverlays() owns the visibility/manager/gate guards for previews.
	const QPoint GlobalPos = QCursor::pos();
	d->updateDropOverlays(GlobalPos);
	const int BorderSize = (frameSize().width() - size().width()) / 2;
	d->updateDragPreviewHeaders(GlobalPos - d->DragStartMousePosition
		- QPoint(BorderSize, 0));
}


//============================================================================
void CFloatingDragPreview::startFloating(const QPoint &DragStartMousePos,
    const QSize &Size, eDragState DragState, QWidget *MouseEventHandler)
{
	Q_UNUSED(DragState)
	d->DragStartMousePosition = DragStartMousePos;
	if (auto Tab = qobject_cast<CDockWidgetTab*>(MouseEventHandler))
	{
		d->TabDrag = true;
		// Use the real tab width captured at mouse-down. This includes expanded
		// Wizard subtabs, but not unrelated title-bar chrome carried by a
		// whole-area drag ghost.
		d->TabInsertionWidth = qMax(1,
			Tab->dragStartTabWidth() > 0
				? Tab->dragStartTabWidth()
				: Tab->width());
	}
	d->ContentPreviewSize = Size;
	resize(Size);
	moveFloating();
	show();
	if (internal::isWayland())
	{
		// On Wayland the preview is a child widget of the source window; raise
		// it above its siblings. On other platforms it is a Qt::Tool window
		// that is already kept on top, so raising is unnecessary here (keeps
		// non-Wayland behavior unchanged).
		raise();
	}
}


//============================================================================
void CFloatingDragPreview::finishDragging()
{
	ADS_PRINT("CFloatingDragPreview::finishDragging");

	const QPoint GlobalPos = d->cursorPos();
	if (d->TabReorderArea && d->TabReorderArea != d->sourceArea())
	{
		auto TargetArea = d->TabReorderArea.data();
		const int TargetIndex = d->TabReorderIndex;
		d->clearTabReorderPreview();
		cleanupAutoHideContainerWidget(CenterDockWidgetArea);
		TargetArea->dockContainer()->dropWidget(d->Content,
			CenterDockWidgetArea, TargetArea, TargetIndex);
		close();
		d->DockManager->containerOverlay()->hideOverlay();
		d->DockManager->dockAreaOverlay()->hideOverlay();
		return;
	}
	auto DockDropArea = d->DockManager->dockAreaOverlay()->visibleDropAreaUnderCursor(GlobalPos);
	auto ContainerDropArea = d->DockManager->containerOverlay()->visibleDropAreaUnderCursor(GlobalPos);
	bool ValidDropArea = (DockDropArea != InvalidDockWidgetArea)  || (ContainerDropArea != InvalidDockWidgetArea);

	// Non floatable auto hide widgets should stay in its current auto hide
	// state if they are dragged into a floating window
	if (ValidDropArea || d->isContentFloatable())
	{
		cleanupAutoHideContainerWidget(ContainerDropArea);
	}

	if (!d->DropContainer)
	{
		d->createFloatingWidget();
	}
	else if (DockDropArea != InvalidDockWidgetArea)
	{
		d->DropContainer->dropWidget(d->Content, DockDropArea, d->DropContainer->dockAreaAt(GlobalPos),
			d->DockManager->dockAreaOverlay()->tabIndexUnderCursor());
	}
	else if (ContainerDropArea != InvalidDockWidgetArea)
	{
		CDockAreaWidget* DockArea = nullptr;
		// If there is only one single dock area, and we drop into the center
		// then we tabify the dropped widget into the only visible dock area
		if (d->DropContainer->visibleDockAreaCount() <= 1 && CenterDockWidgetArea == ContainerDropArea)
		{
			DockArea = d->DropContainer->dockAreaAt(GlobalPos);
		}

		d->DropContainer->dropWidget(d->Content, ContainerDropArea, DockArea,
			d->DockManager->containerOverlay()->tabIndexUnderCursor());
	}
	else
	{
		d->createFloatingWidget();
	}

	this->close();
	d->DockManager->containerOverlay()->hideOverlay();
	d->DockManager->dockAreaOverlay()->hideOverlay();
}


//============================================================================
void CFloatingDragPreview::cleanupAutoHideContainerWidget(DockWidgetArea ContainerDropArea)
{
	auto DroppedDockWidget = qobject_cast<CDockWidget*>(d->Content);
	auto DroppedArea = qobject_cast<CDockAreaWidget*>(d->Content);
	auto AutoHideContainer = DroppedDockWidget
		? DroppedDockWidget->autoHideDockContainer()
		: DroppedArea->autoHideDockContainer();

	if (!AutoHideContainer)
	{
		return;
	}

	// If the dropped widget is already an auto hide widget and if it is moved
	// to a new side bar location in the same container, then we do not need
	// to cleanup
	if (ads::internal::isSideBarArea(ContainerDropArea)
	&& (d->DropContainer == AutoHideContainer->dockContainer()))
	{
		return;
	}

	AutoHideContainer->cleanupAndDelete();
}


//============================================================================
void CFloatingDragPreview::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event);
	if (d->Hidden)
	{
		return;
	}

	QPainter painter(this);
	if (!d->HeaderOnly
	 && CDockManager::testConfigFlag(CDockManager::DragPreviewShowsContentPixmap))
	{
		painter.save();
		painter.setOpacity(0.28);
		painter.drawPixmap(QPoint(0, 0), d->ContentPreviewPixmap);
		painter.restore();
	}

	// If we do not have a window frame then we paint a QRubberBand like
	// frameless window
	if (!d->HeaderOnly
	 && !CDockManager::testConfigFlag(CDockManager::DragPreviewHasWindowFrame))
	{
		QColor Color = palette().color(QPalette::Active, QPalette::Highlight);
		QPen Pen = painter.pen();
		Pen.setColor(Color.darker(120));
		Pen.setStyle(Qt::SolidLine);
		Pen.setWidth(1);
		Pen.setCosmetic(true);
		painter.setPen(Pen);
		Color = Color.lighter(130);
		Color.setAlpha(64);
		painter.setBrush(Color);
		painter.drawRect(rect().adjusted(0, 0, -1, -1));
	}

	if (!d->HeaderPreviewInOverlay && !d->HeaderPreviewPixmap.isNull())
	{
		const qreal PixelRatio = d->HeaderPreviewPixmap.devicePixelRatio();
		const QSize HeaderSize(
			qRound(d->HeaderPreviewPixmap.width() / PixelRatio),
			qRound(d->HeaderPreviewPixmap.height() / PixelRatio));
		const QRect HeaderRect(QPoint(0, 0), HeaderSize);

		painter.save();
		QColor Shadow = palette().color(QPalette::Shadow);
		Shadow.setAlpha(110);
		painter.setPen(Qt::NoPen);
		painter.setBrush(Shadow);
		painter.drawRoundedRect(HeaderRect.translated(2, 2), 3, 3);

		painter.setOpacity(0.96);
		painter.drawPixmap(QPoint(0, 0), d->HeaderPreviewPixmap);
		painter.setOpacity(1.0);
		QPen HeaderOutline(palette().color(QPalette::Active, QPalette::Highlight));
		HeaderOutline.setWidth(1);
		HeaderOutline.setCosmetic(true);
		painter.setPen(HeaderOutline);
		painter.setBrush(Qt::NoBrush);
		painter.drawRoundedRect(HeaderRect.adjusted(0, 0, -1, -1), 3, 3);
		painter.restore();
	}
}

//============================================================================
void CFloatingDragPreview::onApplicationStateChanged(Qt::ApplicationState state)
{
	if (state != Qt::ApplicationActive)
	{
		disconnect(qApp, SIGNAL(applicationStateChanged(Qt::ApplicationState)),
			this, SLOT(onApplicationStateChanged(Qt::ApplicationState)));
		d->cancelDragging();
	}
}


//============================================================================
bool CFloatingDragPreview::eventFilter(QObject *watched, QEvent *event)
{
	Q_UNUSED(watched);
    if (!d->Canceled && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* e = static_cast<QKeyEvent*>(event);
        if (e->key() == Qt::Key_Escape)
        {
            watched->removeEventFilter(this);
            d->cancelDragging();
        }
    }

    return false;
}



} // namespace ads

//---------------------------------------------------------------------------
// EOF FloatingDragPreview.cpp
