// paditem.h – a pad circle on the edge of an element box (SPEC 8 colours).
//   free src: green ring · linked src: filled green · unknown data: orange ring · free sink: gray ring · linked sink: filled gray
#pragma once

#include <QGraphicsObject>
#include <QVariantAnimation>

#include "elementview.h"

class PadItem : public QGraphicsObject {
public:
    PadItem(const PadView& view, QGraphicsItem* parent);

    const PadView& view() const { return view_; }
    QPointF center() const { return scenePos(); }

    QRectF boundingRect() const override { return QRectF(-10, -10, 20, 20); }
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    PadView view_;
    bool hover_ = false;
    double hoverProgress_ = 0;
    QVariantAnimation hoverAnimation_;
};
