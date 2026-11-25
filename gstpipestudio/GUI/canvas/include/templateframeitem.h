// templateframeitem.h – an expanded template on the canvas: a green rounded border around its members, its name, a
// minimize button, and the template's pads on the border with a short connector to the pad each stands for inside.
// The border follows the boxes (it is computed from them), so it grows while they slide apart. It only takes clicks on
// its border, title and button: inside it, every member behaves as a plain element. Clicking the border selects the
// template as one element. Left alone for a while, it collapses by itself.
#pragma once

#include <QGraphicsObject>
#include <QHash>
#include <QTimer>
#include <QVariantAnimation>

#include "elementview.h"

class CanvasScene;
class PadItem;

// A frame that just closed: its border shrinks into the box the template became, and fades.
class FrameGhost : public QGraphicsObject {
public:
    FrameGhost(const QRectF& from, const QRectF& to);
    QRectF boundingRect() const override { return from_.united(to_).adjusted(-4, -4, 4, 4); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    bool done() const { return animation_.state() == QAbstractAnimation::Stopped; }

private:
    QRectF from_, to_;
    double t_ = 0;
    QVariantAnimation animation_;
};

class TemplateFrameItem : public QGraphicsObject {
public:
    static constexpr int kCollapseAfterMs = 5000;
    static constexpr double kPadding = 24, kTitle = 40;

    TemplateFrameItem(const FrameView& view, CanvasScene* canvas);

    const FrameView& view() const { return view_; }
    const QList<PadItem*>& pads() const { return pads_; }
    QRectF frameRect() const { return rect_; }
    void setInner(PadItem* border, PadItem* inner) { inner_[border] = inner; }  // what a border pad stands for inside
    void updateGeometry();  // after boxes moved: around the members, pads on the border
    void appear();          // a new frame grows from its centre
    void pointer(const QPointF& scenePos);  // the mouse moved on the canvas: inside, the collapse waits
    bool collapsePending() const { return collapse_.isActive(); }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QRectF button() const;  // the minimize button, top right
    QRectF shown() const;   // the rect as drawn: while appearing, scaled about its centre
    void collapseIfIdle();

    FrameView view_;
    CanvasScene* canvas_;
    QList<PadItem*> pads_;
    QHash<PadItem*, PadItem*> inner_;
    QRectF rect_;
    QTimer collapse_;
    double grow_ = 1;
    QVariantAnimation growth_;
    QPointF dragFrom_;
    bool dragging_ = false;
};
