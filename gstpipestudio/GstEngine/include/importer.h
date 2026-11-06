#pragma once

#include "graph.h"
#include <string>
#include <vector>

namespace gps {

struct ImportResult {
    Graph graph;
    std::string nativeName;
    std::vector<std::string> warnings;
};

// Parse a gst-launch pipeline in this process, then inspect its native elements,
// properties and linked pads. A short optional probe lets dynamic pads appear.
// It cannot attach to a GstPipeline owned by another process.
bool importLaunch(const std::string& description, int probeMs, ImportResult* out, std::string* error);

} // namespace gps
