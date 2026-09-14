/*******************************************************************************
** Qt Advanced Docking System
** Copyright (C) 2017 Uwe Kindler
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public
** License along with this library; If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/


//============================================================================
/// \file   DockAreaTabBar.cpp
/// \author Uwe Kindler
/// \date   24.08.2018
/// \brief  Implementation of CDockAreaTabBar class
//============================================================================

//============================================================================
//                                   INCLUDES
//============================================================================
#include "FloatingDragPreview.h"
#include "DockAreaTabBar.h"

#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QDebug>
#include <QBoxLayout>
#include <QApplication>
#include <QtGlobal>
#include <QTimer>
#include <QVector>

#include "FloatingDockContainer.h"
#include "DockAreaWidget.h"
#include "DockOverlay.h"
#include "DockManager.h"
#include "DockWidget.h"
#include "DockWidgetTab.h"

#include <iostream>


namespace ads
{
namespace
{
constexpr int ReorderReverseHysteresis = 4;
}

/**
 * Private data class of CDockAreaTabBar class (pimpl)
 */
struct DockAreaTabBarPrivate
{
	CDockAreaTabBar* _this;
	CDockAreaWidget* DockArea;
	QWidget* TabsContainerWidget;
	QBoxLayout* TabsLayout;
	int CurrentIndex = -1;
	QPointer<CDockWidgetTab> DraggedTab;
	QVector<int> DragReorderBoundaries;
	int CurrentDragRank = -1;
	int LastReorderDirection = 0;

	/**
	 * Private data constructor
	 */
	DockAreaTabBarPrivate(CDockAreaTabBar* _public);

	/**
	 * Update tabs after current index changed or when tabs are removed.
	 * The function reassigns the stylesheet to update the tabs
	 */
	void updateTabs();

	/**
	 * Convenience function to access first tab
	 */
	CDockWidgetTab* firstTab() const {return _this->tab(0);}

	/**
	 * Convenience function to access last tab
	 */
	CDockWidgetTab* lastTab() const {return _this->tab(_this->count() - 1);}

	/**
	 * Moves a sibling once the dragged tab covers one third of its width.
	 */
	bool reorderDraggedTab(CDockWidgetTab* MovingTab, int DraggedLeftX,
		int DragDirection, int DragOriginLeftX);
};
// struct DockAreaTabBarPrivate

//============================================================================
DockAreaTabBarPrivate::DockAreaTabBarPrivate(CDockAreaTabBar* _public) :
	_this(_public)
{

}


//============================================================================
void DockAreaTabBarPrivate::updateTabs()
{
	// Set active TAB and update all other tabs to be inactive
	for (int i = 0; i < _this->count(); ++i)
	{
		auto TabWidget = _this->tab(i);
		if (!TabWidget)
		{
			continue;
		}

		if (i == CurrentIndex)
		{
			TabWidget->show();
			TabWidget->setActiveTab(true);
			// Sometimes the synchronous calculation of the rectangular area fails
			// Therefore we use QTimer::singleShot here to execute the call
			// within the event loop - see #520
			QTimer::singleShot(0, _this, [&, TabWidget]
			{
				_this->ensureWidgetVisible(TabWidget);
			});
		}
		else
		{
			TabWidget->setActiveTab(false);
		}
	}
}


//============================================================================
bool DockAreaTabBarPrivate::reorderDraggedTab(CDockWidgetTab* MovingTab,
	int DraggedLeftX, int DragDirection, int DragOriginLeftX)
{
	if (!MovingTab || DragDirection == 0)
	{
		return false;
	}
	if (DraggedTab != MovingTab)
	{
		DraggedTab = MovingTab;
		DragReorderBoundaries.clear();
		CurrentDragRank = -1;
		LastReorderDirection = 0;

		QVector<QRect> OriginalTabGeometries;
		for (int i = 0; i < _this->count(); ++i)
		{
			auto Tab = _this->tab(i);
			if (!Tab->isVisibleTo(_this))
			{
				continue;
			}

			QRect Geometry = Tab->geometry();
			if (Tab == MovingTab)
			{
				CurrentDragRank = OriginalTabGeometries.size();
				Geometry.moveLeft(DragOriginLeftX);
			}
			OriginalTabGeometries.push_back(Geometry);
		}

		if (CurrentDragRank < 0)
		{
			return false;
		}
		for (int i = 0; i + 1 < OriginalTabGeometries.size(); ++i)
		{
			const QRect SiblingGeometry = (i < CurrentDragRank)
				? OriginalTabGeometries.at(i)
				: OriginalTabGeometries.at(i + 1);
			const int RequiredOverlap = qMax(1,
				(SiblingGeometry.width() + 2) / 3);
			const int Boundary = (i < CurrentDragRank)
				? SiblingGeometry.right() - RequiredOverlap + 1
				: SiblingGeometry.left() + RequiredOverlap
					- MovingTab->width();
			DragReorderBoundaries.push_back(Boundary);
		}
	}

	const int FromIndex = TabsLayout->indexOf(MovingTab);
	if (FromIndex < 0)
	{
		return false;
	}

	int ToIndex = -1;
	if (DragDirection < 0)
	{
		if (CurrentDragRank <= 0)
		{
			return false;
		}
		const int Hysteresis = LastReorderDirection > 0
			? ReorderReverseHysteresis : 0;
		if (DraggedLeftX > DragReorderBoundaries.at(CurrentDragRank - 1)
			- Hysteresis)
		{
			return false;
		}
		for (int i = FromIndex - 1; i >= 0; --i)
		{
			auto SiblingTab = _this->tab(i);
			if (!SiblingTab->isVisibleTo(_this))
			{
				continue;
			}
			ToIndex = i;
			break;
		}
	}
	else
	{
		if (CurrentDragRank >= DragReorderBoundaries.size())
		{
			return false;
		}
		const int Hysteresis = LastReorderDirection < 0
			? ReorderReverseHysteresis : 0;
		if (DraggedLeftX < DragReorderBoundaries.at(CurrentDragRank)
			+ Hysteresis)
		{
			return false;
		}
		for (int i = FromIndex + 1; i < _this->count(); ++i)
		{
			auto SiblingTab = _this->tab(i);
			if (!SiblingTab->isVisibleTo(_this))
			{
				continue;
			}
			ToIndex = i;
			break;
		}
	}

	if (ToIndex < 0)
	{
		return false;
	}

	const QPoint DraggedPosition = MovingTab->pos();
	TabsLayout->removeWidget(MovingTab);
	TabsLayout->insertWidget(ToIndex, MovingTab);
	TabsLayout->activate();
	MovingTab->move(DraggedPosition);
	MovingTab->raise();
	ADS_PRINT("tabMoved from " << FromIndex << " to " << ToIndex);
	Q_EMIT _this->tabMoved(FromIndex, ToIndex);
	_this->setCurrentIndex(ToIndex);
	CurrentDragRank += DragDirection;
	LastReorderDirection = DragDirection;
	return true;
}


//============================================================================
CDockAreaTabBar::CDockAreaTabBar(CDockAreaWidget* parent) :
	QScrollArea(parent),
	d(new DockAreaTabBarPrivate(this))
{
	d->DockArea = parent;
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	setFrameStyle(QFrame::NoFrame);
	setWidgetResizable(true);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	d->TabsContainerWidget = new QWidget();
	d->TabsContainerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	d->TabsContainerWidget->setObjectName("tabsContainerWidget");
	d->TabsLayout = new QBoxLayout(QBoxLayout::LeftToRight);
	d->TabsLayout->setContentsMargins(0, 0, 0, 0);
	d->TabsLayout->setSpacing(0);
	d->TabsLayout->addStretch(1);
	d->TabsContainerWidget->setLayout(d->TabsLayout);
	setWidget(d->TabsContainerWidget);

    setFocusPolicy(Qt::NoFocus);
}


//============================================================================
CDockAreaTabBar::~CDockAreaTabBar()
{
	delete d;
}


//============================================================================
void CDockAreaTabBar::wheelEvent(QWheelEvent* Event)
{
    QCoreApplication::sendEvent(horizontalScrollBar(), Event);
}

//============================================================================
void CDockAreaTabBar::setCurrentIndex(int index)
{
	if (index == d->CurrentIndex)
	{
		return;
	}

	if (index < -1 || index > (count() - 1))
	{
		qWarning() << Q_FUNC_INFO << "Invalid index" << index;
		return;
    }

    Q_EMIT currentChanging(index);
	d->CurrentIndex = index;
	d->updateTabs();
	updateGeometry();
	Q_EMIT currentChanged(index);
}


//============================================================================
int CDockAreaTabBar::count() const
{
	// The tab bar contains a stretch item as last item
	return d->TabsLayout->count() - 1;
}


//===========================================================================
void CDockAreaTabBar::insertTab(int Index, CDockWidgetTab* Tab)
{
	d->TabsLayout->insertWidget(Index, Tab);
	connect(Tab, SIGNAL(clicked()), this, SLOT(onTabClicked()));
	connect(Tab, SIGNAL(closeRequested()), this, SLOT(onTabCloseRequested()));
	connect(Tab, SIGNAL(closeOtherTabsRequested()), this, SLOT(onCloseOtherTabsRequested()));
	connect(Tab, SIGNAL(dragged(int,int,int)), this,
		SLOT(onTabWidgetDragged(int,int,int)));
	connect(Tab, SIGNAL(moved(QPoint)), this, SLOT(onTabWidgetMoved(QPoint)));
	connect(Tab, SIGNAL(elidedChanged(bool)), this, SIGNAL(elidedChanged(bool)));
	Tab->installEventFilter(this);
	Q_EMIT tabInserted(Index);
    if (Index <= d->CurrentIndex)
	{
		setCurrentIndex(d->CurrentIndex + 1);
    }
    else if (d->CurrentIndex == -1)
    {
    	setCurrentIndex(Index);
    }

	updateGeometry();
}


//===========================================================================
void CDockAreaTabBar::removeTab(CDockWidgetTab* Tab)
{
	if (!count())
	{
		return;
	}
    ADS_PRINT("CDockAreaTabBar::removeTab ");
	int NewCurrentIndex = currentIndex();
	int RemoveIndex = d->TabsLayout->indexOf(Tab);
	if (count() == 1)
	{
		NewCurrentIndex = -1;
	}
	if (NewCurrentIndex > RemoveIndex)
	{
		NewCurrentIndex--;
	}
	else if (NewCurrentIndex == RemoveIndex)
	{
		NewCurrentIndex = -1;
		// First we walk to the right to search for the next visible tab
		for (int i = (RemoveIndex + 1); i < count(); ++i)
		{
			if (tab(i)->isVisibleTo(this))
			{
				NewCurrentIndex = i - 1;
				break;
			}
		}

		// If there is no visible tab right to this tab then we walk to
		// the left to find a visible tab
		if (NewCurrentIndex < 0)
		{
			for (int i = (RemoveIndex - 1); i >= 0; --i)
			{
				if (tab(i)->isVisibleTo(this))
				{
					NewCurrentIndex = i;
					break;
				}
			}
		}
	}

	Q_EMIT removingTab(RemoveIndex);
	d->TabsLayout->removeWidget(Tab);
	Tab->disconnect(this);
	Tab->removeEventFilter(this);
    ADS_PRINT("NewCurrentIndex " << NewCurrentIndex);
	if (NewCurrentIndex != d->CurrentIndex)
	{
		setCurrentIndex(NewCurrentIndex);
	}
	else
	{
		d->updateTabs();
	}

	updateGeometry();
}


//===========================================================================
int CDockAreaTabBar::currentIndex() const
{
	return d->CurrentIndex;
}


//===========================================================================
CDockWidgetTab* CDockAreaTabBar::currentTab() const
{
	if (d->CurrentIndex < 0 || d->CurrentIndex >= d->TabsLayout->count())
	{
		return nullptr;
	}
	else
	{
		return qobject_cast<CDockWidgetTab*>(d->TabsLayout->itemAt(d->CurrentIndex)->widget());
	}
}


//===========================================================================
void CDockAreaTabBar::onTabClicked()
{
	CDockWidgetTab* Tab = qobject_cast<CDockWidgetTab*>(sender());
	if (!Tab)
	{
		return;
	}

	int index = d->TabsLayout->indexOf(Tab);
	if (index < 0)
	{
		return;
	}
	setCurrentIndex(index);
 	Q_EMIT tabBarClicked(index);
}


//===========================================================================
void CDockAreaTabBar::onTabCloseRequested()
{
	CDockWidgetTab* Tab = qobject_cast<CDockWidgetTab*>(sender());
	int Index = d->TabsLayout->indexOf(Tab);
	closeTab(Index);
}


//===========================================================================
void CDockAreaTabBar::onCloseOtherTabsRequested()
{
	auto Sender = qobject_cast<CDockWidgetTab*>(sender());

    for (int i = count() - 1; i >= 0; --i) {
        auto Tab = tab(i);
        if (Tab->isClosable() && !Tab->isHidden() && Tab != Sender) {
            closeTab(i);
        }
    }
}


//===========================================================================
CDockWidgetTab* CDockAreaTabBar::tab(int Index) const
{
	if (Index >= count() || Index < 0)
	{
		return nullptr;
	}
	return qobject_cast<CDockWidgetTab*>(d->TabsLayout->itemAt(Index)->widget());
}


//===========================================================================
void CDockAreaTabBar::onTabWidgetDragged(int DraggedLeftX, int DragDirection,
	int DragOriginLeftX)
{
	CDockWidgetTab* MovingTab = qobject_cast<CDockWidgetTab*>(sender());
	d->reorderDraggedTab(MovingTab, DraggedLeftX, DragDirection,
		DragOriginLeftX);
}


//===========================================================================
void CDockAreaTabBar::onTabWidgetMoved(const QPoint& GlobalPos)
{
	Q_UNUSED(GlobalPos)
	d->DraggedTab.clear();
	d->DragReorderBoundaries.clear();
	d->CurrentDragRank = -1;
	d->LastReorderDirection = 0;
	// Ensure that the released tab is seated in its layout slot.
	d->TabsLayout->invalidate();
	d->TabsLayout->activate();
}

//===========================================================================
void CDockAreaTabBar::closeTab(int Index)
{
	if (Index < 0 || Index >= count())
	{
		return;
	}

	auto Tab = tab(Index);
	if (Tab->isHidden())
	{
		return;
	}
	Q_EMIT tabCloseRequested(Index);
}


//===========================================================================
bool CDockAreaTabBar::eventFilter(QObject *watched, QEvent *event)
{
	bool Result = Super::eventFilter(watched, event);
	CDockWidgetTab* Tab = qobject_cast<CDockWidgetTab*>(watched);
	if (!Tab)
	{
		return Result;
	}

	switch (event->type())
	{
	case QEvent::Hide:
		 Q_EMIT tabClosed(d->TabsLayout->indexOf(Tab));
		 updateGeometry();
		 break;

	case QEvent::Show:
		 Q_EMIT tabOpened(d->TabsLayout->indexOf(Tab));
		 updateGeometry();
		 break;

    // Setting the text of a tab will cause a LayoutRequest event
    case QEvent::LayoutRequest:
         updateGeometry();
         break;

    // Manage wheel event
    case QEvent::Wheel:
    	// Ignore wheel events if tab is currently dragged
    	if (Tab->dragState() == DraggingInactive)
    	{
    		wheelEvent((QWheelEvent* )event);
    	}
        break;

	default:
		break;
	}

	return Result;
}


//===========================================================================
bool CDockAreaTabBar::isTabOpen(int Index) const
{
	if (Index < 0 || Index >= count())
	{
		return false;
	}

	return !tab(Index)->isHidden();
}


//===========================================================================
QSize CDockAreaTabBar::minimumSizeHint() const
{
	QSize Size = sizeHint();
	Size.setWidth(10);
	return Size;
}


//===========================================================================
QSize CDockAreaTabBar::sizeHint() const
{
	return d->TabsContainerWidget->sizeHint();
}


//===========================================================================
int CDockAreaTabBar::tabAt(const QPoint& Pos) const
{
	if (!isVisible())
	{
		return TabInvalidIndex;
	}

	if (Pos.x() < tab(0)->geometry().x())
	{
		return -1;
	}

	for (int i = 0; i < count(); ++i)
	{
		if (tab(i)->geometry().contains(Pos))
		{
			return i;
		}
	}

	return count();
}


//===========================================================================
int CDockAreaTabBar::tabInsertIndexAt(const QPoint& Pos) const
{
	int Index = tabAt(Pos);
	if (Index == TabInvalidIndex)
	{
		return TabDefaultInsertIndex;
	}
	else
	{
		return (Index < 0) ? 0 : Index;
	}
}


//===========================================================================
bool CDockAreaTabBar::areTabsOverflowing() const
{
	return d->TabsContainerWidget->width() > width();
}

} // namespace ads


//---------------------------------------------------------------------------
// EOF DockAreaTabBar.cpp
