#pragma once
#include <string>
#include <vector>

namespace gps {
struct CapsFieldInfo {
    std::string name, type;  // type: GType name of the value, or of the list/range items
    std::string kind;        // fixed | list | range
    std::vector<std::string> values;
};

struct StructureInfo {
    std::string media;
    std::vector<std::string> features;
    std::vector<CapsFieldInfo> fields;
};

struct PadReport {
    std::string element, pad, identity, templateName, presence;  // presence: always|sometimes|request
    bool isSrc = false, linked = false, temporaryEnd = false, unknownData = false;
    std::string caps, allowed;
    std::vector<StructureInfo> allowedStructures;
};

struct UpdateResult {
    std::string status = "ok";  // ok | error | timeout | pad-missing
    std::string message, errorElement;
    double ms = 0;
    std::vector<PadReport> pads;
    std::vector<std::string> missing;  // "element.identity" the Graph wants and GStreamer never made
    std::vector<std::string> refused;  // "src.pad -> sink.pad: reason"

    bool complete() const;
};
} // namespace gps
