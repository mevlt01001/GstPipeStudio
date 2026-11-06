#include "actions.h"
#include <algorithm>
#include <cmath>

namespace gps {
bool MoveElementsAction::apply(Graph& graph, std::string* error) const {
    bool changed = false;
    for (const auto& placement : positions_) {
        auto* e = graph.find(placement.element);
        if (!e || !std::isfinite(placement.x) || !std::isfinite(placement.y)) {
            if (error) *error = "A moved element is missing or its coordinates are invalid";
            return false;
        }
        changed = changed || e->x != placement.x || e->y != placement.y;
        e->x = placement.x; e->y = placement.y;
    }
    return changed;
}
std::string DisconnectAction::unavailableReason(const Graph& graph) const {
    return graph.linkFrom(element_,pad_) < 0 ? "The connection no longer exists" : "";
}
bool DisconnectAction::apply(Graph& graph, std::string*) const { return graph.disconnectLink(element_,pad_); }
std::string InsertFragmentAction::unavailableReason(const Graph& graph) const {
    if (graph.linkFrom(target_.first,target_.second) < 0) return "The target connection no longer exists";
    if (!fragment_.connected(graph)) return "Connect the selected elements before inserting them";
    if (inputs_.size() > 1 || outputs_.size() > 1 || (inputs_.empty() && outputs_.empty()))
        return "Insertion needs an unambiguous input and output boundary";
    const auto& ids = fragment_.elements();
    const auto& target = graph.links.at(graph.linkFrom(target_.first,target_.second));
    auto selected = [&](const std::string& id){return std::find(ids.begin(),ids.end(),id)!=ids.end();};
    if (selected(target.srcElement) || selected(target.sinkElement)) return "The target connection must be outside the selection";
    for (const auto* ports : {&inputs_, &outputs_})
        for (const auto& port : *ports) if (!selected(port.first)) return "A boundary port is outside the selection";
    return {};
}
bool InsertFragmentAction::apply(Graph& graph, std::string*) const {
    return graph.spliceChain(target_.first,target_.second,inputs_,outputs_);
}
} // namespace gps
