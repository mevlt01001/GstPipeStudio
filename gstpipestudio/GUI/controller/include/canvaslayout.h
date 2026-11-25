// canvaslayout.h – nice alignment: one rule that places every box on the canvas. The canvas becomes a layout::Node
// tree (Composite): a plain element and a collapsed template are boxes with their pads as ports, an expanded template is
// a frame whose inside is built by the same rule, with the template's free pads on its border. The engine's answer comes
// back as one undoable move, plus the link routes and frame borders the canvas draws while nothing has moved since.
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

class CanvasLayout : public QObject {
    Q_OBJECT
public:
    explicit CanvasLayout(Controller& controller);

    // Lays out the selected elements (with the templates they are in), or the whole canvas when the list is empty.
    // Asynchronous: aligned() follows. False when there is nothing to lay out or editing is blocked.
    bool align(const QStringList& elements = {});
    bool busy() const { return pending_ != 0; }

    layout::Routing routing() const { return routing_; }
    void setRouting(layout::Routing routing);  // remembered; the next layout uses it

    // The last answer, while the canvas is as it left it.
    bool fresh() const;
    QList<QList<QPointF>> route(const QString& linkId) const;
    struct Frame {
        QRectF rect;
        QHash<QString, QPointF> ports;  // pad key -> centre on the border
    };
    std::optional<Frame> frame(const QString& instanceId) const;
    void invalidate() { valid_ = false; }  // a template was expanded or collapsed: frames and routes are old

    // The canvas as the engine sees it. Public for tests. Port ids are the canvas pad keys ("<element>|<pad>|src");
    // a frame's own ports are "<instance>|<pad key>"; a link's id is linkId().
    layout::Node tree(const QStringList& elements = {}) const;
    static QString linkId(const QString& srcElement, const QString& srcPad) { return srcElement + "|" + srcPad; }
    static QString padKey(const QString& element, const QString& identity, bool isSrc);

signals:
    void aligned(bool ok);
    void applied(const QHash<QString, QPointF>& from);  // where the moved boxes were: the canvas animates from there

private:
    void finished(quint64 ticket, bool ok, const layout::Result& result, const QString& error);
    QStringList containers(const QString& element, QString* box) const;  // expanded templates around it, outermost first

    Controller& controller_;
    layout::Runner runner_;
    layout::Routing routing_ = layout::Routing::Curves;
    quint64 pending_ = 0;
    QStringList only_;            // the selection the pending request lays out, empty for the whole canvas
    layout::Result result_;       // in canvas coordinates
    std::uint64_t revision_ = 0;  // the document revision the result belongs to
    bool valid_ = false;
};
