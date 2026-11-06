#pragma once
#include <QGraphicsObject>
#include <QPainterPath>
#include <QVariantAnimation>
class ConnectionPreview final : public QGraphicsObject {
  public:
    ConnectionPreview();
    void setEndpoints(const QPointF& start, const QPointF& end, bool source);
    void retract();
    QRectF boundingRect() const override { return path_.boundingRect().adjusted(-5, -5, 5, 5); }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;

  private:
    QPointF start_, end_;
    bool source_ = true;
    QPainterPath path_;
    QVariantAnimation animation_;
    void rebuild();
};
