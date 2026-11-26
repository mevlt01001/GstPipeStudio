// nodes.cpp – see nodes.h.
#include "nodes.h"

#include <algorithm>
#include <set>

#include "pipeline.h"
#include "templates.h"

namespace gps {

namespace {
const UpdateResult& nothingObserved() {
    static const UpdateResult none;
    return none;
}
}  // namespace

std::vector<PadRef> NodePorts::free(Pad::Direction side) const {
    std::vector<PadRef> out;
    for (const Port& p : side == Pad::Direction::Source ? srcs : sinks) {
        if (p.free()) out.push_back(p.pad);
    }
    return out;
}

NodeShape::NodeShape(const Graph& graph, DefinitionFn definitions, const UpdateResult* observed)
    : graph_(graph), definitions_(std::move(definitions)), observed_(observed ? observed : &nothingObserved()) {}

NodeShape::NodeShape(const Graph& graph, const Pipeline& document)
    : NodeShape(graph, [&document](const std::string& factory) { return document.definition(factory); }, &document.observations()) {}

std::vector<Pad> NodeShape::padsOf(const Element& element) const {
    return gps::padsOf(graph_, element, definitions_(element.factory), *observed_);
}

std::vector<std::string> NodeShape::members(const NodeRef& node) const {
    if (node.kind == NodeRef::Kind::Template) return templates::members(graph_, node.id);
    return graph_.find(node.id) ? std::vector<std::string>{node.id} : std::vector<std::string>();
}

// A pad is shown unless its link stays inside the group. A new Request pad is named by its template (PadRef::linkName).
NodePorts NodeShape::ports(const std::vector<std::string>& members) const {
    const std::set<std::string> inside(members.begin(), members.end());
    NodePorts out;
    for (const std::string& name : members) {
        const Element* e = graph_.find(name);
        if (!e) continue;
        for (const Pad& pad : padsOf(*e)) {
            const PadRef ref{e->name, pad.identity, pad.direction};
            const int link = ref.isSrc() ? graph_.linkFrom(e->name, pad.identity) : graph_.linkInto(e->name, pad.identity);
            if (link >= 0) {
                const Connection& l = graph_.links[static_cast<size_t>(link)];
                if (inside.count(ref.isSrc() ? l.sinkElement : l.srcElement)) continue;
            }
            (ref.isSrc() ? out.srcs : out.sinks).push_back({ref, link});
        }
        if (const FactoryDefinition* definition = definitions_(e->factory)) {
            for (const Pad& t : definition->templates) {
                if (t.direction == Pad::Direction::Sink && t.presence == Pad::Presence::Request)
                    out.requests.push_back({e->name, t.templateName, Pad::Direction::Sink});
            }
        }
    }
    return out;
}

Role NodeShape::role(const NodeRef& node) const {
    if (node.kind == NodeRef::Kind::Element) {
        const Element* e = graph_.find(node.id);
        const FactoryDefinition* definition = e ? definitions_(e->factory) : nullptr;
        if (!definition) return Role::Filter;
        return definition->isSource ? Role::Source : definition->isSink ? Role::Sink : Role::Filter;
    }
    const NodePorts p = ports(node);
    const bool in = !p.sinks.empty() || !p.requests.empty(), out = !p.srcs.empty();
    return in == out ? Role::Filter : in ? Role::Sink : Role::Source;
}

std::optional<PadRef> NodeShape::firstFree(const NodeRef& node, Pad::Direction side) const {
    const NodePorts p = ports(node);
    const std::vector<PadRef> free = p.free(side);
    if (!free.empty()) return free.front();
    if (side == Pad::Direction::Sink && !p.requests.empty()) return p.requests.front();
    return std::nullopt;
}

}  // namespace gps
