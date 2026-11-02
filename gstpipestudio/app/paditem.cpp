// paditem.cpp
#include "paditem.h"

#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include "canvasscene.h"
#include "theme.h"

PadItem::PadItem(const PadView& view, QGraphicsItem* parent) : QGraphicsItem(parent), view_(view) {
    setAcceptHoverEvents(true);
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
    double r = metrics::kPadRadius + (hover_ ? 1.5 : 0);
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(colour, 2));
    p->setBrush(filled ? QBrush(colour) : QBrush(Theme::background()));
    p->drawEllipse(QPointF(0, 0), r, r);
}

void PadItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hover_ = true;
    update();
}

void PadItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hover_ = false;
    update();
}

void PadItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    event->accept();  // keeps the element from being dragged by its pad
}

void PadItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    auto* canvas = qobject_cast<CanvasScene*>(scene());
    if (!canvas || !contains(event->pos())) return;
    canvas->padClicked(view_, event->button(), event->screenPos());
}
