// canvasview.cpp
#include "canvasview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
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
    setDragMode(QGraphicsView::RubberBandDrag);
    setRubberBandSelectionMode(Qt::IntersectsItemShape);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // middle-drag or Space+drag pans; left-drag rubber-band selects
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
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit deletePressed();
    } else if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spacePressed_ = true;
        viewport()->setCursor(Qt::OpenHandCursor);
    } else {
        QGraphicsView::keyPressEvent(event);
    }
}

void CanvasView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spacePressed_ = false;
        if (!panning_) viewport()->unsetCursor();
    } else {
        QGraphicsView::keyReleaseEvent(event);
    }
}

void CanvasView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && spacePressed_)) {
        panning_ = true;
        lastPanPos_ = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        QPoint delta = event->pos() - lastPanPos_;
        lastPanPos_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        if (spacePressed_) viewport()->setCursor(Qt::OpenHandCursor);
        else viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
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
