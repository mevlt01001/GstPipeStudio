// elementitem.cpp
#include "elementitem.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include "canvasscene.h"
#include "paditem.h"
#include "theme.h"

using metrics::kBoxWidth;
using metrics::kHeader;
using metrics::kPadRow;
using metrics::kPropLine;

namespace {
double padY(int row) { return kHeader + 6 + (row + 0.5) * kPadRow; }
}  // namespace

ElementItem::PadLabel ElementItem::padLabel(const PadView& pad) {
    QFont small = QApplication::font();
    small.setPointSizeF(8);
    QFontMetrics sm(small);
    int half = int(kBoxWidth / 2 - 16);
    // The name and the media type each get a line of their own, so a long media type does not push the name out.
    return {sm.elidedText(pad.label, Qt::ElideRight, half), sm.elidedText(pad.media, Qt::ElideRight, half)};
}

// An error text longer than the note area ends in "…" (the tooltip has all of it).
QString ElementItem::fitNote(const QString& note, const QFont& font) {
    QFontMetrics fm(font);
    int width = int(kBoxWidth - 24);
    QString text = note;
    auto lines = [&](const QString& t) { return fm.boundingRect(QRect(0, 0, width, 10000), Qt::TextWordWrap | Qt::TextWrapAnywhere, t).height() / fm.lineSpacing(); };
    while (lines(text) > metrics::kNoteLines && text.size() > 4) text = text.left(text.size() - 4).trimmed() + "…";
    return text;
}

ElementItem::ElementItem(const ElementView& view) : view_(view) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setZValue(1);
    setToolTip(view.note.isEmpty() ? view.factory : view.factory + "\n\n" + view.note);

    for (int i = 0; i < view.sinks.size(); ++i) {
        auto* pad = new PadItem(view.sinks[i], this);
        pad->setPos(0, padY(i));
        pads_ << pad;
    }
    for (int i = 0; i < view.srcs.size(); ++i) {
        auto* pad = new PadItem(view.srcs[i], this);
        pad->setPos(kBoxWidth, padY(i));
        pads_ << pad;
    }
}

void ElementItem::clearTelemetry() {
    if (telemetry_) prepareGeometryChange();
    telemetry_ = false; processingMs_ = dropRatePercent_ = -1;
    update();
}

void ElementItem::mergeTelemetry(double processingMs, double dropRatePercent, bool sourceInterval) {
    sourceInterval_ = sourceInterval;
    if (!telemetry_) prepareGeometryChange();
    telemetry_ = true;
    processingMs_ = qMax(processingMs_, processingMs);
    dropRatePercent_ = qMax(dropRatePercent_, dropRatePercent);
    update();
}

QRectF ElementItem::boundingRect() const { return QRectF(-3, -3, kBoxWidth + 6, view_.height() + (telemetry_ ? 42 : 6)); }

QPainterPath ElementItem::shape() const {
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, kBoxWidth, view_.height()), 6, 6);
    return path;
}

void ElementItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);
    QRectF box(0, 0, kBoxWidth, view_.height());

    QColor border = Theme::line();
    double borderWidth = 1;
    if (view_.isError && !view_.note.isEmpty()) border = Theme::error(), borderWidth = 2;
    else if (!view_.note.isEmpty()) border = Theme::warning(), borderWidth = 2;
    else if (isSelected()) border = Theme::accent(), borderWidth = 2;
    else if (hover_) border = Theme::accentHover();

    // Body, header band (clipped to the rounded box), NVIDIA or Custom stripe; the border goes on top of all of it.
    p->setPen(Qt::NoPen);
    p->setBrush(Theme::box());
    p->drawRoundedRect(box, 6, 6);
    p->save();
    p->setClipPath(shape());
    p->fillRect(QRectF(0, 0, kBoxWidth, kHeader), view_.isCustomGroup ? QColor("#263526") : QColor("#333333"));
    p->restore();
    p->setPen(Theme::line());
    p->drawLine(QPointF(1, kHeader), QPointF(kBoxWidth - 1, kHeader));
    if (view_.isCustomGroup) p->fillRect(QRectF(1, 6, 4, kHeader - 12), Theme::accentHover());
    else if (view_.nvidia) p->fillRect(QRectF(1, 8, 3, kHeader - 16), Theme::accent());
    p->setPen(QPen(border, borderWidth));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(box, 6, 6);

    QFont base = p->font();
    QFont bold = base;
    bold.setBold(true);
    QFont small = base;
    small.setPointSizeF(8);

    QString headerTitle = view_.displayTitle.isEmpty() ? view_.name : view_.displayTitle;
    p->setFont(bold);
    p->setPen(view_.isCustomGroup ? Theme::accentHover() : Theme::text());
    p->drawText(QRectF(12, 5, kBoxWidth - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(bold).elidedText(headerTitle, Qt::ElideRight, int(kBoxWidth - 24)));
    p->setFont(small);
    p->setPen(Theme::muted());
    QString subTitle = view_.factory;
    if (!view_.isCustomGroup && !view_.groupId.isEmpty()) {
        subTitle += "  [" + view_.groupId.section('#', 0, 0) + "]";
    }
    p->drawText(QRectF(12, 24, kBoxWidth - 24, 16), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetrics(small).elidedText(subTitle, Qt::ElideRight, int(kBoxWidth - 24)));

    // Pad labels: name, then the media type. Sinks on the left half, srcs on the right half.
    QFontMetrics sm(small);
    int half = int(kBoxWidth / 2 - 16);
    auto label = [&](const PadView& pad, int row) {
        PadLabel text = padLabel(pad);
        double y = padY(row) - kPadRow / 2;
        double x = pad.isSrc ? kBoxWidth / 2 + 4 : 12;
        int align = (pad.isSrc ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
        bool linked = pad.state == PadView::Linked;
        if (text.media.isEmpty()) {
            p->setPen(linked ? Theme::text() : Theme::muted());
            p->drawText(QRectF(x, y, half + 4, kPadRow), align, text.name);
            return;
        }
        p->setPen(linked ? Theme::text() : Theme::muted());
        p->drawText(QRectF(x, y + 1, half + 4, kPadRow / 2), align, text.name);
        p->setPen(Theme::muted());
        p->drawText(QRectF(x, y + kPadRow / 2 - 1, half + 4, kPadRow / 2), align, text.media);
    };
    p->setFont(small);
    for (int i = 0; i < view_.sinks.size(); ++i) label(view_.sinks[i], i);
    for (int i = 0; i < view_.srcs.size(); ++i) label(view_.srcs[i], i);

    // Changed properties.
    double y = kHeader + 6 + qMax(view_.sinks.size(), view_.srcs.size()) * kPadRow + 4;
    p->setPen(Theme::muted());
    for (const QString& line : view_.propLines) {
        p->drawText(QRectF(12, y, kBoxWidth - 24, kPropLine), Qt::AlignLeft | Qt::AlignVCenter,
                    sm.elidedText(line, Qt::ElideRight, int(kBoxWidth - 24)));
        y += kPropLine;
    }

    // Error (red), warning (orange), or info (muted/accent) text: up to kNoteLines lines.
    if (!view_.note.isEmpty()) {
        y += 4;
        p->setPen(view_.isError ? Theme::error() : (view_.isInfo ? Theme::muted() : Theme::warning()));
        p->drawText(QRectF(12, y, kBoxWidth - 24, metrics::kNoteLines * kPropLine),
                    Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere, fitNote(view_.note, small));
    }
    if (telemetry_) {
        const double top = view_.height() + 6;
        p->setPen(Qt::NoPen); p->setBrush(Theme::panel());
        p->drawRoundedRect(QRectF(0, top, kBoxWidth, 31), 4, 4);
        p->setFont(small);
        p->setPen(Theme::text());
        const QString process = processingMs_ >= 0 ? QString::number(processingMs_, 'f', 1) + " ms" : "—";
        const QString drop = dropRatePercent_ >= 0 ? QString::number(dropRatePercent_, 'f', 1) + "%" : "—";
        p->drawText(QRectF(8, top + 2, kBoxWidth - 16, 15), Qt::AlignLeft,
                    (view_.isCustomGroup ? "Max Avg. Time  " : sourceInterval_ ? "Avg. Source Interval  " : "Avg. Processing Time  ") + process);
        p->setPen(Theme::muted());
        p->drawText(QRectF(8, top + 16, kBoxWidth - 16, 15), Qt::AlignLeft,
                    (view_.isCustomGroup ? "Max Drop Rate  " : "Drop Rate  ") + drop);
    }
}

QVariant ElementItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged) {
        if (auto* canvas = qobject_cast<CanvasScene*>(scene())) canvas->updateLinks();
    }
    return QGraphicsItem::itemChange(change, value);
}

void ElementItem::hoverEnterEvent(QGraphicsSceneHoverEvent*) {
    hover_ = true;
    update();
}

void ElementItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    hover_ = false;
    update();
}

void ElementItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    pressPos_ = pos();
    if (event->button() == Qt::RightButton) {
        if (!isSelected()) {
            if (scene() && event->modifiers() == Qt::NoModifier) scene()->clearSelection();
            setSelected(true);
        }
        event->accept();
        return;
    }
    QGraphicsItem::mousePressEvent(event);
    event->accept();  // a press must reach the release below
}

void ElementItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        if (auto* canvas = qobject_cast<CanvasScene*>(scene())) canvas->elementContext(view_.name, event->screenPos());
        event->accept();
        return;
    }
    QGraphicsItem::mouseReleaseEvent(event);
    if (pos() != pressPos_) {
        if (auto* canvas = qobject_cast<CanvasScene*>(scene())) canvas->elementDragged(view_.name, pos());
    } else if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier) {
        if (auto* canvas = qobject_cast<CanvasScene*>(scene())) {
            if (canvas->selectedElementNames().size() <= 1 && !view_.isCustomGroup) {
                canvas->elementDoubleClicked(view_.name);
            }
        }
    }
}

void ElementItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent*) {
    if (auto* canvas = qobject_cast<CanvasScene*>(scene())) canvas->elementDoubleClicked(view_.name);
}
