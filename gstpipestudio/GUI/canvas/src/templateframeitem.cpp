// templateframeitem.cpp – see templateframeitem.h.
#include "templateframeitem.h"

#include <QGraphicsSceneMouseEvent>
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
}  // namespace

FrameGhost::FrameGhost(const QRectF& from, const QRectF& to) : from_(from), to_(to) {
    setZValue(0.5);
    setAcceptedMouseButtons(Qt::NoButton);
    animation_.setDuration(260);
    animation_.setEasingCurve(QEasingCurve::OutCubic);
    animation_.setStartValue(0.0);
    animation_.setEndValue(1.0);
    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        t_ = v.toDouble();
        update();
    });
    animation_.start();
}

void FrameGhost::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    auto mix = [this](double a, double b) { return a + (b - a) * t_; };
    const QRectF r(QPointF(mix(from_.left(), to_.left()), mix(from_.top(), to_.top())),
                   QPointF(mix(from_.right(), to_.right()), mix(from_.bottom(), to_.bottom())));
    p->setRenderHint(QPainter::Antialiasing);
    p->setOpacity(1 - t_);
    p->setPen(QPen(Theme::accent(), 2));
    p->setBrush(Qt::NoBrush);
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
    growth_.setDuration(320);
    growth_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&growth_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        grow_ = v.toDouble();
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

void TemplateFrameItem::appear() {
    growth_.stop();
    growth_.setStartValue(0.15);
    growth_.setEndValue(1.0);
    growth_.start();
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

QRectF TemplateFrameItem::button() const { return QRectF(rect_.right() - 34, rect_.top() + 8, 24, 20); }

QRectF TemplateFrameItem::shown() const {
    if (grow_ >= 1) return rect_;
    const QPointF c = rect_.center();
    return QRectF(c - QPointF(rect_.width(), rect_.height()) * grow_ / 2, rect_.size() * grow_);
}

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
    const QRectF r = shown();
    p->setOpacity(grow_);
    QColor fill = Theme::accent();
    fill.setAlpha(isSelected() ? 26 : 12);
    p->setPen(QPen(Theme::accent(), isSelected() ? 3 : 2));
    p->setBrush(fill);
    p->drawRoundedRect(r, kRadius, kRadius);
    if (grow_ < 1) return;

    QFont title = p->font();
    title.setBold(true);
    p->setFont(title);
    p->setPen(Theme::accent());
    p->drawText(QRectF(r.left() + 16, r.top() + 8, r.width() - 64, 20), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(title).elidedText(view_.title, Qt::ElideRight, int(r.width() - 64)));
    // The minimize button.
    const QRectF b = button();
    p->setPen(QPen(Theme::accent(), 1.5));
    p->setBrush(Theme::background());
    p->drawRoundedRect(b, 4, 4);
    p->drawLine(QPointF(b.left() + 7, b.center().y()), QPointF(b.right() - 7, b.center().y()));
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
