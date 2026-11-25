// templateframeitem.cpp – see templateframeitem.h.
#include "templateframeitem.h"
#include "headerbutton.h"

#include <QGraphicsSceneMouseEvent>
#include <cmath>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSet>

#include "canvasscene.h"
#include "controller.h"
#include "elementitem.h"
#include "paditem.h"
#include "theme.h"

namespace {
constexpr double kRadius = 14;
constexpr double kBand = 14;  // how wide the border is for the mouse
constexpr int kTraceMs = TemplateFrameItem::kTraceMs;

// The border of r as far as it is drawn: from its top left corner along both arms at once, each arm reaching half of
// `share` of the way round, so at 1 the two arms meet at the bottom right and the border is whole.
QPainterPath traced(const QRectF& r, double share) {
    QPainterPath whole;
    whole.addRoundedRect(r, kRadius, kRadius);
    QPainterPath out;
    if (share <= 0) return out;
    if (share >= 1) return whole;
    constexpr int kSteps = 160;
    const double start = whole.percentAtLength(kRadius * 0.6);  // the corner arc of the top left corner
    for (int arm : {1, -1}) {
        for (int i = 0; i <= kSteps; ++i) {
            double at = start + arm * share / 2 * i / kSteps;
            at -= std::floor(at);
            const QPointF point = whole.pointAtPercent(at);
            if (i == 0) out.moveTo(point);
            else out.lineTo(point);
        }
    }
    return out;
}

void run(QVariantAnimation& animation, int ms, double from, double to, QEasingCurve curve) {
    animation.stop();
    animation.setDuration(ms);
    animation.setEasingCurve(curve);
    animation.setStartValue(from);
    animation.setEndValue(to);
    animation.start();
}

}  // namespace

FrameGhost::FrameGhost(const QRectF& from, const QRectF& to, Kind kind) : from_(from), to_(to), kind_(kind) {
    setZValue(0.5);
    setAcceptedMouseButtons(Qt::NoButton);
    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        t_ = v.toDouble();
        update();
    });
    if (kind == Shrink) run(animation_, 260, 0, 1, QEasingCurve::OutCubic);
    else run(animation_, kTraceMs, 0, 1, QEasingCurve::InOutCubic);
}

void FrameGhost::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(Theme::accent(), 2));
    p->setBrush(Qt::NoBrush);
    if (kind_ == Untrace) {  // the border goes back the way it came: both arms withdraw to the corner they started at
        p->drawPath(traced(from_, 1 - t_));
        return;
    }
    auto mix = [this](double a, double b) { return a + (b - a) * t_; };
    const QRectF r(QPointF(mix(from_.left(), to_.left()), mix(from_.top(), to_.top())),
                   QPointF(mix(from_.right(), to_.right()), mix(from_.bottom(), to_.bottom())));
    p->setOpacity(1 - t_);
    p->drawRoundedRect(r, kRadius, kRadius);
}

TemplateFrameItem::TemplateFrameItem(const FrameView& view, CanvasScene* canvas) : view_(view), canvas_(canvas) {
    setFlags(ItemIsSelectable);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setZValue(0.5);  // under the boxes and links, over the grid
    setToolTip(view.title + " · template\nDrag the border to move it, − to collapse it");
    for (const PadView& p : view.pads) pads_ << new PadItem(p, this);
    collapse_.setSingleShot(true);
    collapse_.setInterval(kCollapseAfterMs);
    connect(&collapse_, &QTimer::timeout, this, [this] { collapseIfIdle(); });
    collapse_.start();
    connect(&trace_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        drawn_ = v.toDouble();
        update();
    });
}

void TemplateFrameItem::updateGeometry() {
    QRectF around;
    QSet<QGraphicsItem*> boxes;
    for (const QString& m : view_.members) {
        if (ElementItem* item = canvas_->element(m)) boxes.insert(item);
    }
    for (QGraphicsItem* item : boxes) {
        auto* box = static_cast<ElementItem*>(item);
        const QRectF r(box->pos(), QSizeF(metrics::kBoxWidth, box->view().height()));
        around = around.isNull() ? r : around.united(r);
    }
    for (TemplateFrameItem* inner : canvas_->frames()) {
        if (inner->view().depth > view_.depth && view_.members.contains(inner->view().members.value(0)))
            around = around.united(inner->frameRect());
    }
    prepareGeometryChange();
    rect_ = around.adjusted(-kPadding, -kTitle, kPadding, kPadding);
    // The last nice alignment put the border pads where its routes meet them; elsewhere they sit level with their pad.
    const auto laid = canvas_->controller()->canvasLayout().frame(view_.id);
    const bool asLaid = laid && qAbs(laid->rect.left() - rect_.left()) < 2 && qAbs(laid->rect.top() - rect_.top()) < 2 &&
                        qAbs(laid->rect.width() - rect_.width()) < 2;
    for (PadItem* pad : std::as_const(pads_)) {
        const PadView& p = pad->view();
        const QString key = CanvasLayout::padKey(p.element, p.identity, p.isSrc);
        if (asLaid && laid->ports.contains(key)) {
            pad->setPos(laid->ports.value(key));
            continue;
        }
        PadItem* inner = inner_.value(pad);
        pad->setPos(p.isSrc ? rect_.right() : rect_.left(), inner ? inner->center().y() : rect_.center().y());
    }
    update();
}

void TemplateFrameItem::trace(qint64 elapsed) {
    if (elapsed >= kTraceMs) return;
    drawn_ = double(elapsed) / kTraceMs;
    run(trace_, int(kTraceMs - elapsed), drawn_, 1, QEasingCurve::OutCubic);
}

void TemplateFrameItem::pointer(const QPointF& scenePos) {
    if (rect_.contains(scenePos)) collapse_.stop();
    else if (!collapse_.isActive()) collapse_.start();
}

void TemplateFrameItem::collapseIfIdle() {
    bool busy = isSelected() || canvas_->locked();
    for (const QString& m : view_.members) {
        if (ElementItem* item = canvas_->element(m)) busy = busy || item->isSelected();
    }
    if (busy) collapse_.start();  // something inside is being worked on: wait another round
    else canvas_->frameMinimized(view_.id);
}

QRectF TemplateFrameItem::button() const { return headerbutton::at(rect_.right() - 10, rect_.top() + 8, 0); }

QRectF TemplateFrameItem::boundingRect() const { return rect_.adjusted(-kBand, -kBand, kBand, kBand); }

QPainterPath TemplateFrameItem::shape() const {
    QPainterPath border;
    border.addRoundedRect(rect_, kRadius, kRadius);
    QPainterPathStroker stroker;
    stroker.setWidth(kBand);
    QPainterPath out = stroker.createStroke(border);
    out.addRect(QRectF(rect_.left(), rect_.top(), rect_.width(), kTitle - 8));  // the title band
    return out.simplified();
}

void TemplateFrameItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect_;
    p->setPen(QPen(Theme::accent(), isSelected() ? 3 : 2));
    if (drawn_ < 1) {  // being traced: the border only, as far as it is drawn
        p->setBrush(Qt::NoBrush);
        p->drawPath(traced(r, drawn_));
        return;
    }
    QColor fill = Theme::accent();
    fill.setAlpha(isSelected() ? 26 : 12);
    p->setBrush(fill);
    p->drawRoundedRect(r, kRadius, kRadius);

    QFont title = p->font();
    title.setBold(true);
    p->setFont(title);
    p->setPen(Theme::accent());
    const double titleWidth = button().left() - 4 - (r.left() + 16);
    p->drawText(QRectF(r.left() + 16, r.top() + 8, titleWidth, 20), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(title).elidedText(view_.title, Qt::ElideRight, int(titleWidth)));
    headerbutton::paint(p, button(), headerbutton::Glyph::Minus, Theme::accent());
    // A short connector from every border pad to the pad it stands for inside.
    QPen connector(Theme::accent(), 1.4, Qt::DashLine);
    p->setPen(connector);
    p->setBrush(Qt::NoBrush);
    for (PadItem* pad : std::as_const(pads_)) {
        PadItem* inner = inner_.value(pad);
        if (!inner) continue;
        const QPointF a = inner->center(), z = pad->center();
        QPainterPath line(a);
        const double reach = qAbs(z.x() - a.x()) / 2;
        const double dir = pad->view().isSrc ? 1 : -1;
        line.cubicTo(a + QPointF(dir * reach, 0), z - QPointF(dir * reach, 0), z);
        p->drawPath(line);
    }
}

void TemplateFrameItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (canvas_->locked()) return;
    if (!isSelected()) {
        if (!(event->modifiers() & Qt::ControlModifier)) scene()->clearSelection();
        setSelected(true);
    }
    dragging_ = false;
    dragFrom_ = event->scenePos();
    event->accept();
}

void TemplateFrameItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton) || button().contains(dragFrom_) || canvas_->locked()) return;
    const QPointF delta = event->scenePos() - dragFrom_;
    if (!dragging_ && delta.manhattanLength() < 3) return;
    dragging_ = true;
    dragFrom_ = event->scenePos();
    QSet<ElementItem*> moved;
    for (const QString& m : view_.members) {
        ElementItem* item = canvas_->element(m);
        if (item && !moved.contains(item)) {
            moved.insert(item);
            item->setPos(item->pos() + delta);
        }
    }
    canvas_->updateLinks();
}

void TemplateFrameItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        canvas_->frameContext(view_.id, event->screenPos());
    } else if (dragging_) {
        if (ElementItem* first = canvas_->element(view_.members.value(0))) canvas_->elementDragged(first->view().name, first->pos());
    } else if (button().contains(event->scenePos())) {
        canvas_->frameMinimized(view_.id);
    }
    dragging_ = false;
    event->accept();
}

void TemplateFrameItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (!button().contains(event->scenePos())) canvas_->frameActivated(view_.id);
}
