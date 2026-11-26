// nodes.h – what is placed on a canvas as one thing (an element or a template copy; later a plugin node) and its shape:
// the pads it shows to the outside, which of them are free, and whether it starts, passes on or ends a stream.
// Worked out from the factory definitions and the links alone, so it answers for a draft as well as for the document.
// Pure data like graph.h: no Qt, no GStreamer.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "graph.h"
#include "runtime.h"

namespace gps {

class Pipeline;

// One thing on the canvas: an element by its name, or a template copy by its instance id.
struct NodeRef {
    enum class Kind { Element, Template };
    Kind kind = Kind::Element;
    std::string id;

    static NodeRef element(std::string name) { return {Kind::Element, std::move(name)}; }
    static NodeRef instance(std::string id) { return {Kind::Template, std::move(id)}; }
    bool operator==(const NodeRef& other) const { return kind == other.kind && id == other.id; }
};

enum class Role { Source, Filter, Sink };

// A pad a node shows, and the link on it (index in Graph::links, -1 when it is free).
struct Port {
    PadRef pad;
    int link = -1;
    bool free() const { return link < 0; }
};

// The pads a node shows to the outside: linked to something outside it, or free. requests are new pads its members'
// Request sink templates would give: another way in when no sink pad is free.
struct NodePorts {
    std::vector<Port> sinks, srcs;
    std::vector<PadRef> requests;

    std::vector<PadRef> free(Pad::Direction side) const;
};

class NodeShape {
public:
    using DefinitionFn = std::function<const FactoryDefinition*(const std::string& factory)>;
    // observed: the last run, which names the Sometimes pads it saw; none for a template that never ran.
    NodeShape(const Graph& graph, DefinitionFn definitions, const UpdateResult* observed = nullptr);
    NodeShape(const Graph& graph, const Pipeline& document);  // the document's definitions and its last run

    std::vector<std::string> members(const NodeRef& node) const;  // an element is its own only member
    NodePorts ports(const NodeRef& node) const { return ports(members(node)); }
    NodePorts ports(const std::vector<std::string>& members) const;  // of any group of elements
    // An element: its factory (a source has no sink template, a sink no src template). A template copy: its ports.
    Role role(const NodeRef& node) const;
    // The pad a new link on this side would use: the first free one, else a new pad of a Request sink template.
    std::optional<PadRef> firstFree(const NodeRef& node, Pad::Direction side) const;

private:
    std::vector<Pad> padsOf(const Element& element) const;

    const Graph& graph_;
    DefinitionFn definitions_;
    const UpdateResult* observed_;
};

}  // namespace gps
