// linkitem.cpp
#include "linkitem.h"

#include <QFontMetrics>
#include <QLineF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

#include "canvasmetrics.h"
#include "capslabel.h"
#include "canvasscene.h"
#include "paditem.h"
#include "theme.h"

LinkItem::LabelLayout LinkItem::decideLayout(double pipeLength, double singleLineWidth, double zoomLevel) {
    if (singleLineWidth <= 0.0) return Hidden;
    if (zoomLevel >= 1.4 || pipeLength >= 3.0 * singleLineWidth) return AtPads;
    if (pipeLength >= singleLineWidth) return SingleLine;
    return MultiLine;
}

LinkItem::LinkItem(const LinkView& view, PadItem* from, PadItem* to) : view_(view), from_(from), to_(to) {
    setFlags(ItemIsSelectable);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setToolTip(view.tip);
    setZValue(1.5);  // above the boxes: the badge must never be hidden behind a neighbour
    updatePath();
}

void LinkItem::updatePath() {
    prepareGeometryChange();
    // The line starts and ends at the pad's edge, so the pad circles stay visible.
    const double edge = metrics::kPadRadius + 2;
    const QPointF from = from_->center(), to = to_->center();
    auto at = [](QPointF p, QPointF q) { return QLineF(p, q).length() < 1.5; };
    // The route of the last nice alignment, while both pads are where it left them. Its pieces inside expanded templates
    // belong to their frames (the connectors from border pads); the link draws the piece between its own two pads.
    QList<QPointF> piece;
    for (const QList<QPointF>& candidate : view_.route) {
        if (candidate.size() >= 2 && at(candidate.front(), from) && at(candidate.back(), to)) piece = candidate;
    }
    path_ = QPainterPath();
    if (!piece.isEmpty()) {
        piece.front() += QPointF(edge, 0);
        piece.back() -= QPointF(edge, 0);
        path_.moveTo(piece.front());
        for (int i = 1; i < piece.size(); ++i) path_.lineTo(piece[i]);
    } else {
        const QPointF a = from + QPointF(edge, 0), b = to - QPointF(edge, 0);
        const double reach = qBound(30.0, qAbs(b.x() - a.x()) / 2, 150.0);
        path_.moveTo(a);
        path_.cubicTo(a + QPointF(reach, 0), b - QPointF(reach, 0), b);
    }
    update();
}

QRectF LinkItem::badgeRect() const {
    if (view_.badge.isEmpty()) return QRectF();
    QFont font;
    font.setPointSizeF(8);
    QFontMetrics fm(font);
    QPointF mid = midPoint();
    // Below the line, not on it: the gap between two boxes is only 40 px, a chip on the line would cover both pad circles.
    return QRectF(mid.x() - fm.horizontalAdvance(view_.badge) / 2.0 - 6, mid.y() + metrics::kPadRadius + 2, fm.horizontalAdvance(view_.badge) + 12, 16);
}

QRectF LinkItem::boundingRect() const {
    return path_.controlPointRect().adjusted(-120, -60, 120, 60).united(badgeRect().adjusted(-2, -2, 2, 2));
}

QPainterPath LinkItem::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(12);
    QPainterPath s = stroker.createStroke(path_);
    if (!view_.badge.isEmpty()) s.addRect(badgeRect());
    return s;
}

void LinkItem::paint(QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    p->setRenderHint(QPainter::Antialiasing);
    bool selected = isSelected();
    p->setPen(QPen(hover_ || selected ? Theme::accentHover() : QColor("#5f9400"), hover_ || selected ? 3 : 2));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path_);

    if (!view_.badge.isEmpty()) {
        QFont font = p->font();
        font.setPointSizeF(8);
        p->setFont(font);
        p->setPen(QPen(Theme::accent(), 1));
        p->setBrush(Theme::background());
        QRectF rect = badgeRect();
        p->drawRoundedRect(rect, 8, 8);
        p->setPen(Theme::text());
        p->drawText(rect, Qt::AlignCenter, view_.badge);
    }

    QString caps = view_.negotiatedCaps;
    if (!caps.isEmpty()) {
        double lod = option ? QStyleOptionGraphicsItem::levelOfDetailFromTransform(p->worldTransform()) : 1.0;
        if (lod <= 0.0) lod = 1.0;
        QString single = capslabel::singleLine(caps);
        QString multi = capslabel::multiLine(caps);
        QFont baseFont = capslabel::font(p->font());
        QFontMetricsF fm(baseFont);
        double wSingle = fm.horizontalAdvance(single);
        double lPipe = pipeLength();
        LabelLayout layout = decideLayout(lPipe, wSingle, lod);

        if (hover_) {
            QPointF mid = midPoint();
            QRectF textRect = fm.boundingRect(QRectF(0, 0, 320, 200), Qt::AlignLeft | Qt::TextWordWrap, multi).adjusted(-6, -4, 6, 4);
            textRect.moveCenter(mid - QPointF(0, textRect.height() / 2 + 12));
            p->setFont(baseFont);
            p->setPen(QPen(Theme::accent(), 1));
            p->setBrush(Theme::panel());
            p->drawRoundedRect(textRect, 4, 4);
            p->setPen(Theme::text());
            p->drawText(textRect.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::TextWordWrap, multi);
        } else if (widget != nullptr) {
            if (layout == SingleLine) {
                QPointF mid = midPoint();
                QRectF r(mid.x() - wSingle / 2 - 4, mid.y() - 18, wSingle + 8, 14);
                p->setFont(baseFont);
                p->setPen(Theme::muted());
                p->drawText(r, Qt::AlignCenter, single);
            } else if (layout == MultiLine && lPipe >= 70.0) {
                QPointF mid = midPoint();
                QRectF r = fm.boundingRect(QRectF(0, 0, 200, 120), Qt::AlignCenter, multi);
                r.moveCenter(mid - QPointF(0, r.height() / 2 + 8));
                p->setFont(baseFont);
                p->setPen(Theme::muted());
                p->drawText(r, Qt::AlignCenter, multi);
            } else if (layout == AtPads && (lPipe >= 3.0 * wSingle || lod >= 1.4)) {
                // Scale-invariant font at pads: keeps constant screen size as the pad grows on zoom-in.
                QFont padFont = p->font();
                padFont.setPointSizeF(7.5 / lod);
                p->setFont(padFont);
                p->setPen(Theme::muted());
                QFontMetricsF pfm(padFont);
                QRectF r = pfm.boundingRect(QRectF(0, 0, 240 / lod, 160 / lod), Qt::AlignLeft, multi);
                QPointF a = from_->center() + QPointF(10 / lod, -r.height() - 4 / lod);
                QPointF b = to_->center() + QPointF(-r.width() - 10 / lod, -r.height() - 4 / lod);
                p->drawText(QRectF(a, r.size()), Qt::AlignLeft, multi);
                if (lPipe >= 3.0 * wSingle) {
                    p->drawText(QRectF(b, r.size()), Qt::AlignRight, multi);
                }
            }
        }
    }
}

void LinkItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hover_ = true;
    update();
}

void LinkItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hover_ = false;
    update();
}

void LinkItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mousePressEvent(event);
    event->accept();
}

void LinkItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsItem::mouseReleaseEvent(event);
    auto* canvas = qobject_cast<CanvasScene*>(scene());
    if (!canvas) return;
    if (event->button() == Qt::LeftButton) {
        canvas->linkClicked(view_.srcElement, view_.srcIdentity);
    } else if (event->button() == Qt::RightButton) {
        canvas->linkContext(view_.srcElement, view_.srcIdentity, event->screenPos());
    }
}
