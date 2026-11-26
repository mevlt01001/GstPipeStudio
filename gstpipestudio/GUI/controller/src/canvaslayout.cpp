// canvaslayout.cpp – see canvaslayout.h.
#include "canvaslayout.h"

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
    connect(&runner_, &layout::Runner::finished, this, &CanvasLayout::finished);
    // Loading ELK takes a moment: do it now, so the first real layout does not wait for it.
    layout::Node warm;
    warm.id = "root";
    layout::Node one;
    one.id = "warm";
    one.width = one.height = 10;
    warm.children << one;
    runner_.request(warm, preferences().routing());
}

const ViewPreferences& CanvasLayout::preferences() const { return controller_.services().view(); }

QRectF CanvasLayout::boxRect(const QString& box) const {
    const gps::Element* at = controller_.plan().find(toStd(controller_.outline().anchorOf(box)));
    if (!at) return QRectF();
    const ElementView v = at->name == toStd(box) ? controller_.view(box) : controller_.templateView(box);
    return QRectF(at->x, at->y, metrics::kBoxWidth, v.height());
}

CanvasLayout::Pin CanvasLayout::pinOf(const QStringList& elements, bool gather) const {
    QList<QRectF> drawn;
    for (const QString& box : controller_.outline().boxesOf(elements)) drawn << boxRect(box);
    return {elements, bounds(drawn).center(), gather};
}

layout::Node CanvasLayout::tree(const QStringList& elements) const {
    const gps::Graph& g = controller_.plan();
    const Outline outline = controller_.outline();
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
        const QString box = outline.boxOf(name);
        const QStringList path = outline.framesAround(name);
        if (placed.contains(box)) continue;
        const bool instance = box != name;
        if (!chosen.isEmpty()) {
            const QStringList inside = outline.membersOf(box);
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
                const QString key = fromStd(p.ref.key());
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
        const QString srcBox = outline.boxOf(src), sinkBox = outline.boxOf(sink);
        const QStringList from = outline.framesAround(src), to = outline.framesAround(sink);
        if (srcBox == sinkBox || !placed.contains(srcBox) || !placed.contains(sinkBox)) continue;
        int shared = 0;
        while (shared < from.size() && shared < to.size() && from[shared] == to[shared]) ++shared;
        const QString id = fromStd(gps::LinkRef{g.srcOf(i)}.id());
        const QString srcKey = fromStd(g.srcOf(i).key()), sinkKey = fromStd(g.sinkOf(i).key());
        const QString a = outward(from, shared, srcKey, true, id, srcKey);
        const QString b = outward(to, shared, sinkKey, false, id, sinkKey);
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

bool CanvasLayout::align(const QStringList& elements, std::optional<Pin> pin) {
    if (controller_.editingBlocked() || controller_.plan().empty()) return false;
    const layout::Node root = tree(elements);
    if (root.children.isEmpty()) return false;
    pin_ = std::move(pin);
    pending_ = runner_.request(root, preferences().routing());
    return true;
}

void CanvasLayout::finished(quint64 ticket, bool ok, const layout::Result& result, const QString& error) {
    if (ticket != pending_) return;  // the warm-up, or an answer nobody waits for any more
    pending_ = 0;
    const std::optional<Pin> pin = std::exchange(pin_, std::nullopt);
    if (!ok) {
        emit controller_.logLine("Layout failed: " + error);
        emit aligned(false);
        return;
    }
    // Where the laid-out boxes go: around the pin's centre, or without one from the top left corner.
    const gps::Graph& g = controller_.plan();
    const Outline outline = controller_.outline();
    QList<QRectF> laid, pinned;
    QHash<QString, QString> anchor;  // box -> the element whose position the canvas draws it at
    const QStringList pinnedBoxes = pin ? outline.boxesOf(pin->elements) : QStringList();
    for (const gps::Element& e : g.elements) {
        const QString box = outline.boxOf(fromStd(e.name));
        if (!result.nodes.contains(box) || anchor.contains(box)) continue;
        anchor[box] = outline.anchorOf(box);
        laid << result.nodes[box];
        if (pinnedBoxes.contains(box)) pinned << result.nodes[box];
    }
    if (anchor.isEmpty()) {
        emit aligned(false);
        return;
    }
    const QPointF shift = pin && !pinned.isEmpty() ? pin->centre - bounds(pinned).center() : QPointF(kMargin, kMargin) - bounds(laid).topLeft();
    result_ = result;
    for (QRectF& r : result_.nodes) r.translate(shift);
    for (QPointF& p : result_.ports) p += shift;
    for (auto& pieces : result_.edges)
        for (auto& piece : pieces)
            for (QPointF& p : piece) p += shift;

    QList<QPair<QString, QPointF>> moves;
    QHash<QString, QPointF> from;  // gathered boxes come from the pin's centre, where the canvas drew them
    for (auto it = anchor.cbegin(); it != anchor.cend(); ++it) {
        const gps::Element* e = g.find(toStd(it.value()));
        const QRectF to = result_.nodes[it.key()];
        from[it.value()] = pin && pin->gather && pinnedBoxes.contains(it.key()) ? pin->centre - QPointF(to.width(), to.height()) / 2 : QPointF(e->x, e->y);
        moves.append({it.value(), to.topLeft()});
    }
    applying_ = true;
    controller_.setPositions(moves);
    applying_ = false;
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
