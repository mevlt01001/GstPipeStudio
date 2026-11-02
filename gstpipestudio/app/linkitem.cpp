// linkitem.cpp
#include "linkitem.h"

#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>

#include "canvasmetrics.h"
#include "canvasscene.h"
#include "paditem.h"
#include "theme.h"

LinkItem::LinkItem(const LinkView& view, PadItem* from, PadItem* to) : view_(view), from_(from), to_(to) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::RightButton);
    setToolTip(view.tip);
    setZValue(1.5);  // above the boxes: the badge must never be hidden behind a neighbour
    updatePath();
}

void LinkItem::updatePath() {
    prepareGeometryChange();
    // The curve starts and ends at the pad's edge, so the pad circles stay visible.
    double edge = metrics::kPadRadius + 2;
    QPointF a = from_->center() + QPointF(edge, 0), b = to_->center() - QPointF(edge, 0);
    double reach = qBound(30.0, qAbs(b.x() - a.x()) / 2, 150.0);
    path_ = QPainterPath(a);
    path_.cubicTo(a + QPointF(reach, 0), b - QPointF(reach, 0), b);
    update();
}

QRectF LinkItem::badgeRect() const {
    if (view_.badge.isEmpty()) return QRectF();
    QFont font;
    font.setPointSizeF(8);
    QFontMetrics fm(font);
    QPointF mid = midPoint();
    // Below the line, not on it: the gap between two boxes is only 40 px, a chip on the line would cover both pad circles.
    return QRectF(mid.x() - fm.horizontalAdvance(view_.badge) / 2.0 - 6, mid.y() + metrics::kPadRadius + 2, fm.horizontalAdvance(view_.badge) + 12, 16);
}

QRectF LinkItem::boundingRect() const {
    return path_.controlPointRect().adjusted(-8, -8, 8, 8).united(badgeRect().adjusted(-2, -2, 2, 2));
}

QPainterPath LinkItem::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(12);
    QPainterPath s = stroker.createStroke(path_);
    if (!view_.badge.isEmpty()) s.addRect(badgeRect());
    return s;
}

void LinkItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(hover_ ? Theme::accentHover() : QColor("#5f9400"), hover_ ? 3 : 2));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path_);

    if (!view_.badge.isEmpty()) {
        QFont font = p->font();
        font.setPointSizeF(8);
        p->setFont(font);
        p->setPen(QPen(Theme::accent(), 1));
        p->setBrush(Theme::background());
        QRectF rect = badgeRect();
        p->drawRoundedRect(rect, 8, 8);
        p->setPen(Theme::text());
        p->drawText(rect, Qt::AlignCenter, view_.badge);
    }
}

void LinkItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hover_ = true;
    update();
}

void LinkItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hover_ = false;
    update();
}

void LinkItem::mousePressEvent(QGraphicsSceneMouseEvent* event) { event->accept(); }

void LinkItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (auto* canvas = qobject_cast<CanvasScene*>(scene())) canvas->linkContext(view_.srcElement, view_.srcIdentity, event->screenPos());
}
