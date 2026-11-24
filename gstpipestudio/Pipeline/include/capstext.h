// capstext.h – the caps text the user's choice becomes (SPEC 4.6). Pure text, no GStreamer.
#pragma once

#include <string>
#include <vector>

#include "runtime.h"

namespace gps {

struct CapsField {
    std::string name;   // "width"
    std::string type;   // GType name: gint, gchararray, GstFraction ...
    std::string value;  // the single value the user pinned
};

// The fields left on Otomatik are simply not here.
struct CapsChoice {
    std::string media;                 // "video/x-raw"; empty means no choice at all
    std::vector<std::string> features; // "memory:NVMM"
    std::vector<CapsField> fields;

    // "video/x-raw(memory:NVMM), width=(int)640", or "" when there is no media type.
    std::string toText() const;
};

// One string value inside caps text: bare only with [A-Za-z0-9_+-./:], otherwise "..." with \", \\ and a space escaped.
std::string capsString(const std::string& value);

// "video/x-raw(memory:NVMM)": the media type and memory features of one structure.
std::string structureTitle(const StructureInfo& s);
// "video/x-raw(memory:NVMM), width=(int)1" -> "video/x-raw(memory:NVMM)"
std::string mediaOf(const std::string& caps);
// Caps meet by media type and memory features; an empty side (ANY) meets everything. A quick filter for what the GUI
// offers; GStreamer's own negotiation decides when the pipeline is validated.
bool structuresMeet(const std::vector<StructureInfo>& a, const std::vector<StructureInfo>& b);

}  // namespace gps
