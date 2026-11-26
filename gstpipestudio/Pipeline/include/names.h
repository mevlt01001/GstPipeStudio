// names.h – every name the user reads instead of an identity: an element ("filesrc 2"), a pad ("filesrc 2.video_0"), a
// template copy (the name of the template it is a copy of) and a parameter ("filesrc 2.location"). Worked out once
// for a graph; copies share it. Pure data like graph.h: no Qt, no GStreamer.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "graph.h"

namespace gps {

class Names {
public:
    Names();
    explicit Names(const Graph& graph);

    // The factory, then "<factory> 2", "<factory> 3"... in canvas order; the name itself when the graph has no such element.
    std::string element(const std::string& name) const;
    // "<element>.<label>", the label read from the graph's pads (a pad the last run named shows that name).
    std::string pad(const Graph& graph, const PadRef& pad) const;
    // What a pad is called on screen: the name the last run gave it, else its identity with the number filled in.
    static std::string label(const Pad& pad);
    // The template it is a copy of, never read from its id; the id when the graph has no such copy.
    std::string instance(const std::string& id) const;
    std::string param(const ExposedParam& p) const;  // "<element>.<property>"

private:
    struct Table {
        std::map<std::string, std::string> elements, instances;
    };
    std::shared_ptr<const Table> table_;
};

}  // namespace gps
