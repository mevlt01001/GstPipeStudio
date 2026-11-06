// paditem.cpp
#include "paditem.h"

#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include "canvasscene.h"
#include "elementitem.h"
#include "theme.h"

PadItem::PadItem(const PadView& view, QGraphicsItem* parent) : QGraphicsObject(parent), view_(view) {
    setAcceptHoverEvents(true);
    hoverAnimation_.setDuration(140); hoverAnimation_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&hoverAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        hoverProgress_ = value.toDouble(); update();
    });
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setZValue(2);
    if (!view.tip.isEmpty()) setToolTip(view.tip);
    bool clickable = view.isSrc && view.state != PadView::Linked;
    if (clickable) setCursor(Qt::PointingHandCursor);
}

QPainterPath PadItem::shape() const {
    QPainterPath path;
    path.addEllipse(QPointF(0, 0), 9, 9);
    return path;
}

void PadItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    QColor colour = Theme::muted();
    bool filled = view_.state == PadView::Linked;
    if (view_.isSrc) {
        colour = view_.state == PadView::Unknown ? Theme::warning() : (hover_ ? Theme::accentHover() : Theme::accent());
    }
    double r = metrics::kPadRadius + (hoverProgress_ * 1.5);
    p->setRenderHint(QPainter::Antialiasing);
    QColor halo = colour; halo.setAlpha(qRound(hoverProgress_*55));
    p->setPen(Qt::NoPen); p->setBrush(halo); p->drawEllipse(QPointF(0,0), r+2, r+2);
    p->setPen(QPen(colour, 2));
    p->setBrush(filled ? QBrush(colour) : QBrush(Theme::background()));
    p->drawEllipse(QPointF(0, 0), r, r);
}

void PadItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hover_ = true;
    hoverAnimation_.stop(); hoverAnimation_.setStartValue(hoverProgress_); hoverAnimation_.setEndValue(1.0); hoverAnimation_.start();
}

void PadItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hover_ = false;
    hoverAnimation_.stop(); hoverAnimation_.setStartValue(hoverProgress_); hoverAnimation_.setEndValue(0.0); hoverAnimation_.start();
}

void PadItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    event->accept();  // keeps the element from being dragged by its pad
}

void PadItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        if (auto* canvas = qobject_cast<CanvasScene*>(scene())) {
            canvas->padDragMoved(view_, event->scenePos());
        }
    }
    event->accept();
}

void PadItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    auto* canvas = qobject_cast<CanvasScene*>(scene());
    if (!canvas) return;
    canvas->padDragEnded();
    if (contains(event->pos())) {
        canvas->padClicked(view_, event->button(), event->screenPos());
        return;
    }
    if (event->button() == Qt::LeftButton) {
        for (QGraphicsItem* item : canvas->items(event->scenePos())) {
            if (auto* target = dynamic_cast<PadItem*>(item)) {
                if (target != this) {
                    canvas->padDroppedOnPad(view_, target->view());
                    return;
                }
            }
        }
        // Dropped directly onto an element box: find its first free sink (or src) pad and link!
        for (QGraphicsItem* item : canvas->items(event->scenePos())) {
            if (auto* elem = dynamic_cast<ElementItem*>(item)) {
                for (PadItem* p : elem->pads()) {
                    if (p->view().isSrc != view_.isSrc && p->view().state != PadView::Linked) {
                        canvas->padDroppedOnPad(view_, p->view());
                        return;
                    }
                }
            }
        }
    }
}
