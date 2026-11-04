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
    void setDimmed(bool dimmed);  // a thin veil while the canvas is locked
    bool dimmed() const { return dimmed_; }

signals:
    void deletePressed();  // the Delete key with the canvas focused

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    bool dimmed_ = false;
};
