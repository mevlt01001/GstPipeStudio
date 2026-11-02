// canvasview.h – the QGraphicsView: grid background, wheel zoom, drag to pan.
#pragma once

#include <QGraphicsView>

class CanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit CanvasView(QGraphicsScene* scene, QWidget* parent = nullptr);

    double zoom() const { return transform().m11(); }
    void zoomBy(double factor);
    void fitAll();  // whole pipeline in view, never magnified past 1:1

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
};
