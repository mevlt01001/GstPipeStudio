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

// A frame that just went away. Closed, its border shrinks into the box the template became and fades; gone (deleted,
// detached, cancelled), its border is erased the way it was drawn.
class FrameGhost : public QGraphicsObject {
public:
    enum Kind { Shrink, Untrace };
    FrameGhost(const QRectF& from, const QRectF& to, Kind kind);
    QRectF boundingRect() const override { return from_.united(to_).adjusted(-4, -4, 4, 4); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    bool done() const { return animation_.state() == QAbstractAnimation::Stopped; }
    Kind kind() const { return kind_; }

private:
    QRectF from_, to_;
    Kind kind_;
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
    // A new template: its border is drawn from one corner along both arms, then it fills. A frame built again while
    // that goes on carries on from where the last one was (`elapsed` ms into it).
    void trace(qint64 elapsed = 0);
    static constexpr int kTraceMs = 650;
    void pointer(const QPointF& scenePos);  // the mouse moved on the canvas: inside, the collapse waits
    bool collapsePending() const { return collapse_.isActive(); }
    bool tracing() const { return trace_.state() == QAbstractAnimation::Running; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QRectF button() const;      // the minimize button, top right
    QRectF editButton() const;  // "✎", left of it
    bool onButton(const QPointF& at) const { return button().contains(at) || editButton().contains(at); }
    void collapseIfIdle();

    FrameView view_;
    CanvasScene* canvas_;
    QList<PadItem*> pads_;
    QHash<PadItem*, PadItem*> inner_;
    QRectF rect_;
    QTimer collapse_;
    double drawn_ = 1;  // how much of the border is drawn
    QVariantAnimation trace_;
    QPointF dragFrom_;
    bool dragging_ = false;
};
