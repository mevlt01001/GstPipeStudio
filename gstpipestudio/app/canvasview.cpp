// canvasview.cpp
#include "canvasview.h"

#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>

#include "theme.h"

namespace {
constexpr double kMinZoom = 0.2;
constexpr double kMaxZoom = 3.0;
constexpr int kGrid = 20;
}  // namespace

CanvasView::CanvasView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // dragging the background pans
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setObjectName("canvas");
}

void CanvasView::zoomBy(double factor) {
    double target = qBound(kMinZoom, zoom() * factor, kMaxZoom);
    double step = target / zoom();
    scale(step, step);
}

void CanvasView::fitAll() {
    QRectF items = scene()->itemsBoundingRect();
    if (items.isEmpty()) {
        resetTransform();
        return;
    }
    fitInView(items.adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    if (zoom() > 1.0) resetTransform();
    centerOn(items.center());
}

void CanvasView::setDimmed(bool dimmed) {
    if (dimmed_ == dimmed) return;
    dimmed_ = dimmed;
    viewport()->update();
}

void CanvasView::drawForeground(QPainter* p, const QRectF& rect) {
    if (dimmed_) p->fillRect(rect, QColor(0, 0, 0, 70));
}

void CanvasView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) emit deletePressed();
    else QGraphicsView::keyPressEvent(event);
}

void CanvasView::wheelEvent(QWheelEvent* event) {
    zoomBy(std::pow(1.0015, event->angleDelta().y()));
    event->accept();
}

void CanvasView::drawBackground(QPainter* p, const QRectF& rect) {
    p->fillRect(rect, Theme::background());
    p->setPen(QPen(QColor("#242424"), 0));
    double left = std::floor(rect.left() / kGrid) * kGrid;
    double top = std::floor(rect.top() / kGrid) * kGrid;
    for (double x = left; x < rect.right(); x += kGrid) p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (double y = top; y < rect.bottom(); y += kGrid) p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}
