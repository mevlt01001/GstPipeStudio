// linkitem.h – a bezier link between two pads, with a small badge when the link carries a caps choice.
#pragma once

#include <QGraphicsItem>
#include <QPainterPath>

#include "elementview.h"

class PadItem;

class LinkItem : public QGraphicsItem {
public:
    LinkItem(const LinkView& view, PadItem* from, PadItem* to);

    const LinkView& view() const { return view_; }
    void updatePath();  // after an element moved
    QPointF midPoint() const { return path_.pointAtPercent(0.5); }  // where the badge sits

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QRectF badgeRect() const;

    LinkView view_;
    PadItem* from_;
    PadItem* to_;
    QPainterPath path_;
    bool hover_ = false;
};
