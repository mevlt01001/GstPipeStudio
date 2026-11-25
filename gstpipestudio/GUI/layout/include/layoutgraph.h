// layoutgraph.h – what a layout engine is asked and what it answers. Pure geometry: boxes with ports on their left and
// right edges, frames that hold boxes (an expanded template), and links between ports. A frame is a node with children;
// the same rules lay out the canvas and the inside of every frame (Composite).
#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace layout {

struct Port {
    QString id;         // unique in the whole graph
    bool east = false;  // src pads on the right edge, sink pads on the left
    double y = 0;       // a box's port: its centre below the box's top (boxes draw their pads there); a frame chooses
};

struct Edge {
    QString id;          // the same id on every piece of one link
    QString from, to;    // port ids: a box's, or the frame's own ports (from inside to its border and back)
};

struct Node {
    QString id;
    double width = 0, height = 0;  // a frame's size comes from its children
    QList<Port> ports;             // in their order from top to bottom on each edge
    QList<Node> children;          // not empty: a frame
    QList<Edge> edges;             // between the children's ports and this node's own ports
    bool frame() const { return !children.isEmpty(); }
};

enum class Routing { Curves, RightAngles };

struct Result {
    QHash<QString, QRectF> nodes;               // every box and frame, in canvas coordinates
    QHash<QString, QPointF> ports;              // every port's centre
    QHash<QString, QList<QList<QPointF>>> edges;  // per link: its pieces, each a polyline (curves come sampled)
};

// The one interface every engine implements; the GUI never depends on a particular engine.
class Engine {
public:
    virtual ~Engine() = default;
    virtual bool run(const Node& root, Routing routing, Result* out, QString* error) = 0;
};

}  // namespace layout
