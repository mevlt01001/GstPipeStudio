// canvaslayout.h – nice alignment: one rule that places every box on the canvas. The canvas becomes a layout::Node
// tree (Composite): a plain element and a collapsed template are boxes with their pads as ports, an expanded template is
// a frame whose inside is built by the same rule, with the template's free pads on its border. The engine's answer comes
// back as one undoable move, plus the link routes and frame borders the canvas draws while nothing has moved since.
// Nothing on screen jumps: the answer is moved so the boxes it was asked about keep their centre (a pin).
#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <optional>

#include "layoutgraph.h"
#include "layoutrunner.h"

class Controller;
class ViewPreferences;

class CanvasLayout : public QObject {
    Q_OBJECT
public:
    explicit CanvasLayout(Controller& controller);

    // Where the answer lands: the boxes of these elements keep the centre they are drawn around now. Gathered, the
    // canvas draws them at that centre while the answer is on its way and they slide out of it: a template grows out of
    // its centre, or shrinks into it.
    struct Pin {
        QStringList elements;
        QPointF centre;
        bool gather = false;
    };
    Pin pinOf(const QStringList& elements, bool gather = false) const;  // their centre now
    // Lays out the selected elements (with the templates they are in), or the whole canvas when the list is empty.
    // Without a pin the canvas starts at the top left corner (a file just opened). Asynchronous: aligned() follows.
    // False when there is nothing to lay out or editing is blocked.
    bool align(const QStringList& elements = {}, std::optional<Pin> pin = std::nullopt);
    bool busy() const { return pending_ != 0; }
    const std::optional<Pin>& pending() const { return pin_; }  // the pin of the answer on its way
    bool applying() const { return applying_; }  // its move is being made (not an edit of the user's)

    QRectF boxRect(const QString& box) const;  // where a box (Outline) is drawn now

    // The last answer, while the canvas is as it left it.
    bool fresh() const;
    QList<QList<QPointF>> route(const QString& linkId) const;
    struct Frame {
        QRectF rect;
        QHash<QString, QPointF> ports;  // pad key -> centre on the border
    };
    std::optional<Frame> frame(const QString& instanceId) const;
    void invalidate() { valid_ = false; }  // a template was expanded or collapsed: frames and routes are old

    // The canvas as the engine sees it. Public for tests. Port ids are the pad keys (gps::PadRef::key()); a frame's own
    // ports are "<instance>|<pad key>"; a link's id is gps::LinkRef::id().
    layout::Node tree(const QStringList& elements = {}) const;

signals:
    void aligned(bool ok);
    void applied(const QHash<QString, QPointF>& from);  // where the moved boxes were: the canvas animates from there

private:
    const ViewPreferences& preferences() const;  // the link style it routes with
    void finished(quint64 ticket, bool ok, const layout::Result& result, const QString& error);

    Controller& controller_;
    layout::Runner runner_;
    quint64 pending_ = 0;
    std::optional<Pin> pin_;
    bool applying_ = false;
    layout::Result result_;       // in canvas coordinates
    std::uint64_t revision_ = 0;  // the document revision the result belongs to
    bool valid_ = false;
};
