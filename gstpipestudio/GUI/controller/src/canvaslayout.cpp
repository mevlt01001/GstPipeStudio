// canvaslayout.cpp – see canvaslayout.h.
#include "canvaslayout.h"

#include <QSettings>
#include <QSet>
#include <algorithm>

#include "controller.h"
#include "elkengine.h"
#include "templates.h"

namespace {

constexpr double kMargin = 40;  // the whole canvas starts here

QRectF bounds(const QList<QRectF>& rects) {
    QRectF out;
    for (const QRectF& r : rects) out = out.isNull() ? r : out.united(r);
    return out;
}

}  // namespace

CanvasLayout::CanvasLayout(Controller& controller)
    : QObject(&controller), controller_(controller), runner_([] { return std::make_unique<layout::ElkEngine>(); }, this) {
    routing_ = QSettings().value("view/linkStyle").toString() == "rightAngles" ? layout::Routing::RightAngles : layout::Routing::Curves;
    connect(&runner_, &layout::Runner::finished, this, &CanvasLayout::finished);
    // Loading ELK takes a moment: do it now, so the first real layout does not wait for it.
    layout::Node warm;
    warm.id = "root";
    layout::Node one;
    one.id = "warm";
    one.width = one.height = 10;
    warm.children << one;
    runner_.request(warm, routing_);
}

QString CanvasLayout::padKey(const QString& element, const QString& identity, bool isSrc) {
    return element + "|" + identity + (isSrc ? "|src" : "|sink");
}

void CanvasLayout::setRouting(layout::Routing routing) {
    routing_ = routing;
    QSettings().setValue("view/linkStyle", routing == layout::Routing::RightAngles ? "rightAngles" : "curves");
}

QStringList CanvasLayout::containers(const QString& element, QString* box) const {
    QStringList out;
    *box = element;
    const std::vector<std::string> around = gps::templates::chain(controller_.plan(), toStd(element));
    for (auto up = around.rbegin(); up != around.rend(); ++up) {
        const QString id = fromStd(*up);
        if (!controller_.isTemplateExpanded(id)) {
            *box = id;
            break;
        }
        out << id;
    }
    return out;
}

layout::Node CanvasLayout::tree(const QStringList& elements) const {
    const gps::Graph& g = controller_.plan();
    const QSet<QString> chosen(elements.begin(), elements.end());
    QHash<QString, QList<layout::Node>> leaves;   // container ("" = canvas) -> its boxes
    QHash<QString, QList<layout::Edge>> edges;    // container -> link pieces inside it
    QHash<QString, QList<layout::Port>> borders;  // expanded template -> its own ports
    QSet<QString> placed, portIds;
    QHash<QString, QStringList> pathOf;           // box -> expanded templates around it

    auto border = [&](const QString& frame, const QString& key, bool east) {
        const QString id = frame + "|" + key;
        if (!portIds.contains(id)) {
            portIds.insert(id);
            borders[frame].append({id, east, 0});
        }
        return id;
    };
    // A pad inside expanded templates reaches the canvas level through a port on every border around it.
    auto outward = [&](const QStringList& path, int down, const QString& key, bool isSrc, const QString& link, QString end) {
        for (int k = path.size() - 1; k >= down; --k) {
            const QString port = border(path[k], key, isSrc);
            edges[path[k]].append(isSrc ? layout::Edge{link, end, port} : layout::Edge{link, port, end});
            end = port;
        }
        return end;
    };

    for (const gps::Element& e : g.elements) {
        const QString name = fromStd(e.name);
        QString box;
        const QStringList path = containers(name, &box);
        if (placed.contains(box)) continue;
        const bool instance = box != name;
        if (!chosen.isEmpty()) {
            const QStringList inside = instance ? controller_.templateMembers(box) : QStringList{name};
            if (std::none_of(inside.begin(), inside.end(), [&](const QString& m) { return chosen.contains(m); })) continue;
        }
        placed.insert(box);
        pathOf[box] = path;
        const ElementView v = instance ? controller_.templateView(box) : controller_.view(name);
        layout::Node leaf;
        leaf.id = box;
        leaf.width = metrics::kBoxWidth;
        leaf.height = v.height();
        auto pads = [&](const QList<PadView>& list, bool east) {
            for (int i = 0; i < list.size(); ++i) {
                const PadView& p = list[i];
                const QString key = padKey(p.element, p.identity, p.isSrc);
                leaf.ports.append({key, east, metrics::padY(i)});
                if (p.state != PadView::Linked && !path.isEmpty()) outward(path, 0, key, east, "free|" + key, key);
            }
        };
        pads(v.sinks, false);
        pads(v.srcs, true);
        leaves[path.isEmpty() ? QString() : path.last()].append(leaf);
    }

    for (size_t i = 0; i < g.links.size(); ++i) {
        const gps::Connection& l = g.links[i];
        const QString src = fromStd(l.srcElement), sink = fromStd(l.sinkElement);
        QString srcBox, sinkBox;
        const QStringList from = containers(src, &srcBox), to = containers(sink, &sinkBox);
        if (srcBox == sinkBox || !placed.contains(srcBox) || !placed.contains(sinkBox)) continue;
        int shared = 0;
        while (shared < from.size() && shared < to.size() && from[shared] == to[shared]) ++shared;
        const QString id = linkId(src, fromStd(l.srcPad));
        const QString a = outward(from, shared, padKey(src, fromStd(l.srcPad), true), true, id, padKey(src, fromStd(l.srcPad), true));
        const QString b = outward(to, shared, padKey(sink, fromStd(g.sinkIdentity(i)), false), false, id,
                                  padKey(sink, fromStd(g.sinkIdentity(i)), false));
        edges[shared ? from[shared - 1] : QString()].append({id, a, b});
    }

    // Frames hold their leaves and the frames inside them; a frame with nothing placed inside is left out.
    std::function<layout::Node(const QString&)> build = [&](const QString& container) {
        layout::Node n;
        n.id = container.isEmpty() ? "root" : container;
        n.children = leaves.value(container);
        for (const gps::TemplateInstance& t : g.instances) {
            const QString id = fromStd(t.id);
            if (!controller_.isTemplateExpanded(id) || fromStd(t.parent) != container) continue;
            if (!t.parent.empty() && !controller_.isTemplateExpanded(fromStd(t.parent))) continue;
            layout::Node frame = build(id);
            if (!frame.children.isEmpty()) n.children.append(frame);
        }
        n.edges = edges.value(container);
        n.ports = borders.value(container);
        return n;
    };
    return build(QString());
}

bool CanvasLayout::align(const QStringList& elements) {
    if (controller_.editingBlocked() || controller_.plan().empty()) return false;
    const layout::Node root = tree(elements);
    if (root.children.isEmpty()) return false;
    only_ = elements;
    pending_ = runner_.request(root, routing_);
    return true;
}

void CanvasLayout::finished(quint64 ticket, bool ok, const layout::Result& result, const QString& error) {
    if (ticket != pending_) return;  // the warm-up, or an answer nobody waits for any more
    pending_ = 0;
    if (!ok) {
        emit controller_.logLine("Layout failed: " + error);
        emit aligned(false);
        return;
    }
    // Where the laid-out boxes go: the whole canvas from the top left corner, a selection where it already was.
    const gps::Graph& g = controller_.plan();
    QList<QRectF> laid, before;
    QHash<QString, QString> anchor;  // box -> the element whose position the canvas draws it at
    for (const gps::Element& e : g.elements) {
        const QString name = fromStd(e.name);
        QString box;
        containers(name, &box);
        if (!result.nodes.contains(box) || anchor.contains(box)) continue;
        anchor[box] = name;
        laid << result.nodes[box];
        before << QRectF(QPointF(e.x, e.y), result.nodes[box].size());
    }
    if (anchor.isEmpty()) {
        emit aligned(false);
        return;
    }
    const QPointF origin = only_.isEmpty() ? QPointF(kMargin, kMargin) : bounds(before).topLeft();
    const QPointF shift = origin - bounds(laid).topLeft();
    result_ = result;
    for (QRectF& r : result_.nodes) r.translate(shift);
    for (QPointF& p : result_.ports) p += shift;
    for (auto& pieces : result_.edges)
        for (auto& piece : pieces)
            for (QPointF& p : piece) p += shift;

    QList<QPair<QString, QPointF>> moves;
    QHash<QString, QPointF> from;
    for (auto it = anchor.cbegin(); it != anchor.cend(); ++it) {
        const gps::Element* e = g.find(toStd(it.value()));
        from[it.value()] = QPointF(e->x, e->y);
        moves.append({it.value(), result_.nodes[it.key()].topLeft()});
    }
    controller_.setPositions(moves);
    revision_ = controller_.pipeline().lastChange().revision;
    valid_ = true;
    emit applied(from);
    emit aligned(true);
}

bool CanvasLayout::fresh() const { return valid_ && controller_.pipeline().lastChange().revision == revision_; }

QList<QList<QPointF>> CanvasLayout::route(const QString& linkId) const {
    return fresh() ? result_.edges.value(linkId) : QList<QList<QPointF>>();
}

std::optional<CanvasLayout::Frame> CanvasLayout::frame(const QString& instanceId) const {
    if (!fresh() || !result_.nodes.contains(instanceId)) return std::nullopt;
    Frame f;
    f.rect = result_.nodes[instanceId];
    const QString prefix = instanceId + "|";
    for (auto it = result_.ports.cbegin(); it != result_.ports.cend(); ++it) {
        if (it.key().startsWith(prefix) && it.key().count('|') >= 3) f.ports[it.key().mid(prefix.size())] = it.value();
    }
    return f;
}
