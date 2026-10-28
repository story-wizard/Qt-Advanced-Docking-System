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
/// \file   DockSplitter.cpp
/// \author Uwe Kindler
/// \date   24.03.2017
/// \brief  Implementation of CDockSplitter
//============================================================================

//============================================================================
//                                   INCLUDES
//============================================================================
#include "DockSplitter.h"

#include <QDebug>
#include <QChildEvent>
#include <QVariant>
#include <QMouseEvent>
#include <QPointer>
#include <QPainter>
#include <QVariantAnimation>
#include <QTimer>
#include <QSplitterHandle>
#include "DockAreaWidget.h"

namespace ads
{
class DockSplitterHandle : public QSplitterHandle
{
    struct Target
    {
        QPointer<DockSplitterHandle> handle;
        QPoint offset;
    };
    QList<Target> targets;
    bool intersectionActive = false;
    bool intersectionHovered = false;
    bool pointerHovered = false;
    bool pressed = false;
    QVariantAnimation highlightAnimation;
    QTimer highlightDelay;
    qreal highlightOpacity = 0.0;
    bool highlightVisible = false;

    void updateHighlight()
    {
        const bool visible = intersectionActive || intersectionHovered || pointerHovered || pressed;
        if (visible == highlightVisible) return;
        highlightVisible = visible;
        highlightDelay.stop();
        if (visible && highlightOpacity <= 0.0)
        {
            highlightDelay.start();
            return;
        }
        animateHighlight();
    }

    void animateHighlight()
    {
        highlightAnimation.stop();
        highlightAnimation.setStartValue(highlightOpacity);
        highlightAnimation.setEndValue(highlightVisible ? 1.0 : 0.0);
        highlightAnimation.start();
    }
    QList<QPointer<DockSplitterHandle>> previewTargets;

    void setPreview(const QList<DockSplitterHandle*>& handles)
    {
        QList<QPointer<DockSplitterHandle>> next;
        for (auto* handle : handles) next.append(handle);
        for (const auto& handle : previewTargets)
            if (handle && !next.contains(handle))
            {
                handle->intersectionHovered = false;
                handle->updateHighlight();
            }
        previewTargets = next;
        for (const auto& handle : previewTargets)
            if (handle)
            {
                handle->intersectionHovered = true;
                handle->updateHighlight();
            }
    }

    void clearTargets()
    {
        const auto previous = targets;
        targets.clear();
        for (const auto& target : previous)
            if (auto* h = target.handle.data())
            {
                static_cast<CDockSplitter*>(h->splitter())->setRubberBand(-1);
                h->intersectionActive = false;
                h->updateHighlight();
            }
    }

    QList<DockSplitterHandle*> nearby(const QPoint& global) const
    {
        auto* root = splitter();
        while (auto* parent = qobject_cast<CDockSplitter*>(root->parentWidget()))
            root = parent;
        QList<DockSplitterHandle*> result;
        QList<CDockSplitter*> splitters{static_cast<CDockSplitter*>(root)};
        for (int n = 0; n < splitters.size(); ++n)
            for (int i = 0; i < splitters[n]->count(); ++i)
                if (auto* child = qobject_cast<CDockSplitter*>(splitters[n]->widget(i)))
                    splitters.append(child);
        bool horizontal = false, vertical = false;
        for (auto* s : splitters)
            for (int i = 1; i < s->count(); ++i)
            {
                auto* h = dynamic_cast<DockSplitterHandle*>(s->handle(i));
                if (!h || !h->isVisible() || !h->isEnabled()) continue;
                const QRect bounds(h->mapToGlobal(QPoint()), h->size());
                const int margin = qMax(6, splitter()->handleWidth() + 1);
                if (!bounds.adjusted(-margin, -margin, margin, margin).contains(global)) continue;
                result.append(h);
                horizontal |= h->orientation() == Qt::Horizontal;
                vertical |= h->orientation() == Qt::Vertical;
            }
        return horizontal && vertical ? result : QList<DockSplitterHandle*>();
    }

    bool moveTargets(const QPoint& global, bool commit)
    {
        const auto activeTargets = targets;
        QPointer<DockSplitterHandle> guard(this);
        for (const auto& target : activeTargets)
        {
            auto* h = target.handle.data();
            if (!h || !h->isVisible() || (commit && h->opaqueResize())) continue;
            const auto local = h->splitter()->mapFromGlobal(global - target.offset);
            const int rtlOffset = h->orientation() == Qt::Horizontal
                && h->splitter()->isRightToLeft() ? h->width() : 0;
            const int position = h->orientation() == Qt::Horizontal ? local.x() + rtlOffset : local.y();
            if (commit || h->opaqueResize()) h->moveSplitter(position);
            else static_cast<CDockSplitter*>(h->splitter())->setRubberBand(
                h->closestLegalPosition(position) - rtlOffset);
            if (!guard) return false;
            if (targets.isEmpty()) break;
        }
        return true;
    }

    void finish()
    {
        pressed = false;
        clearTargets();
        updateHighlight();
        if (mouseGrabber() == this) releaseMouse();
        setCursor(orientation() == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QSplitterHandle::paintEvent(event);
        if (highlightOpacity > 0.0)
        {
            QPainter painter(this);
            painter.setOpacity(highlightOpacity);
            painter.fillRect(rect(), palette().color(QPalette::Active, QPalette::Highlight));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            pressed = true;
            updateHighlight();
            const auto global = internal::globalPositionOf(event);
            const auto handles = nearby(global);
            setPreview({});
            for (auto* h : handles)
            {
                targets.append({h, global - h->mapToGlobal(QPoint())});
                h->intersectionActive = true;
                h->updateHighlight();
            }
            if (!targets.isEmpty())
            {
                setCursor(Qt::SizeAllCursor);
                grabMouse();
                event->accept();
                return;
            }
        }
        QSplitterHandle::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        pointerHovered = rect().contains(event->pos());
        updateHighlight();
        if (!targets.isEmpty())
        {
            if (!(event->buttons() & Qt::LeftButton)) { finish(); return; }
            if (!moveTargets(internal::globalPositionOf(event), false)) return;
            event->accept();
            return;
        }
        const auto handles = nearby(internal::globalPositionOf(event));
        setPreview(handles);
        setCursor(handles.isEmpty()
            ? (orientation() == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor)
            : Qt::SizeAllCursor);
        QSplitterHandle::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) { pressed = false; updateHighlight(); }
        if (event->button() == Qt::LeftButton && !targets.isEmpty())
        {
            if (!moveTargets(internal::globalPositionOf(event), true)) return;
            finish();
            event->accept();
            return;
        }
        QSplitterHandle::mouseReleaseEvent(event);
    }

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide
            || event->type() == QEvent::WindowDeactivate)
        {
            pressed = false;
            intersectionHovered = false;
            pointerHovered = false;
            clearTargets();
            setPreview({});
            updateHighlight();
        }
        else if (event->type() == QEvent::Leave)
        {
            pointerHovered = false;
            setPreview({});
            updateHighlight();
        }
        else if (event->type() == QEvent::Enter)
        {
            pointerHovered = true;
            updateHighlight();
        }
        return QSplitterHandle::event(event);
    }

public:
    ~DockSplitterHandle() override { clearTargets(); setPreview({}); }

    DockSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent)
    {
        setMouseTracking(true);
        setProperty("ads-splitter-highlight-managed", true);
        highlightDelay.setSingleShot(true);
        highlightDelay.setInterval(200);
        connect(&highlightDelay, &QTimer::timeout, this, [this] { animateHighlight(); });
        highlightAnimation.setDuration(120);
        highlightAnimation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&highlightAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            highlightOpacity = value.toReal();
            update();
        });
    }
};

QSplitterHandle* CDockSplitter::createHandle()
{
    return new DockSplitterHandle(orientation(), this);
}

/**
 * Private dock splitter data
 */
struct DockSplitterPrivate
{
	CDockSplitter* _this;
	int VisibleContentCount = 0;

	DockSplitterPrivate(CDockSplitter* _public) : _this(_public) {}
};

//============================================================================
CDockSplitter::CDockSplitter(QWidget *parent)
	: QSplitter(parent),
	  d(new DockSplitterPrivate(this))
{
    setProperty("ads-splitter", QVariant(true));
	setChildrenCollapsible(false);
}


//============================================================================
CDockSplitter::CDockSplitter(Qt::Orientation orientation, QWidget *parent)
	: QSplitter(orientation, parent),
	  d(new DockSplitterPrivate(this))
{

}

//============================================================================
CDockSplitter::~CDockSplitter()
{
    ADS_PRINT("~CDockSplitter");
	delete d;
}


//============================================================================
bool CDockSplitter::hasVisibleContent() const
{
	// TODO Cache or precalculate this to speed up
	for (int i = 0; i < count(); ++i)
	{
		if (!widget(i)->isHidden())
		{
			return true;
		}
	}

	return false;
}


//============================================================================
QWidget* CDockSplitter::firstWidget() const
{
	return (count() > 0) ? widget(0) : nullptr;
}


//============================================================================
QWidget* CDockSplitter::lastWidget() const
{
	return (count() > 0) ? widget(count() - 1) : nullptr;
}

//============================================================================
bool CDockSplitter::isResizingWithContainer() const
{
    for (auto area : findChildren<CDockAreaWidget*>())
    {
        if(area->isCentralWidgetArea())
        {
            return true;
        }
    }

    return false;
}

} // namespace ads

//---------------------------------------------------------------------------
// EOF DockSplitter.cpp
