// elementitem.h – an element box: name and factory on top, pads on the edges, changed properties below.
#pragma once

#include <QFont>
#include <QGraphicsItem>

#include "elementview.h"

class PadItem;

class ElementItem : public QGraphicsItem {
public:
    explicit ElementItem(const ElementView& view);

    const ElementView& view() const { return view_; }
    const QList<PadItem*>& pads() const { return pads_; }

    // The text drawn for a pad: its name and its media type (elided when it does not fit its half of the box).
    struct PadLabel {
        QString name, media;
    };
    static PadLabel padLabel(const PadView& pad);
    static QString fitNote(const QString& note, const QFont& font);  // the note as drawn: at most kNoteLines lines
    void clearTelemetry();
    void mergeTelemetry(double processingMs, double dropRatePercent, double ageMs = -1);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    ElementView view_;
    QList<PadItem*> pads_;
    bool hover_ = false;
    bool telemetry_ = false;
    double ageMs_ = -1;
    double processingMs_ = -1, dropRatePercent_ = -1;
    QPointF pressPos_;
};
