// boxplacement.cpp
#include "boxplacement.h"

#include <set>

#include "canvasmetrics.h"
#include "graph.h"

namespace BoxPlacement {

void placeNew(gps::Graph& g, const std::vector<std::string>& created, const HeightOf& heightOf) {
    const std::set<std::string> mine(created.begin(), created.end());
    auto boxOf = [&](const gps::Element& e) { return QRectF(e.x, e.y, metrics::kBoxWidth, heightOf(e.name)); };
    std::vector<gps::Element*> moving;
    QRectF block;
    for (const std::string& name : created) {
        if (gps::Element* e = g.find(name)) {
            moving.push_back(e);
            block |= boxOf(*e);
        }
    }
    if (moving.empty()) return;
    const double top = block.top();
    const double margin = metrics::kGap / 2;
    for (bool moved = true; moved;) {
        moved = false;
        for (const gps::Element& other : g.elements) {
            if (mine.count(other.name)) continue;
            const QRectF occupied = boxOf(other);
            if (block.adjusted(-margin, -margin, margin, margin).intersects(occupied)) {
                block.moveTop(occupied.bottom() + metrics::kGap);
                moved = true;
            }
        }
    }
    for (gps::Element* e : moving) e->y += block.top() - top;
}

void makeRoomForQueue(gps::Graph& g, const std::string& source, const std::string& queue) {
    const int out = g.linkFrom(queue, gps::kQueueSrcPad);
    gps::Element* from = g.find(source);
    gps::Element* q = g.find(queue);
    if (out < 0 || !from || !q) return;
    gps::Element* sink = g.find(g.links[static_cast<size_t>(out)].sinkElement);
    if (!sink) return;
    q->x = from->x + metrics::kColumn;
    q->y = sink->y;
    const double need = q->x + metrics::kColumn - sink->x;
    if (need <= 0) return;

    std::set<std::string> behind = {sink->name};
    for (bool grew = true; grew;) {
        grew = false;
        for (const gps::Connection& l : g.links) {
            if (behind.count(l.srcElement) && behind.insert(l.sinkElement).second) grew = true;
        }
    }
    for (gps::Element& e : g.elements) {
        if (behind.count(e.name)) e.x += need;
    }
}

}  // namespace BoxPlacement
