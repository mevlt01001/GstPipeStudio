// capstext.h – the caps text the user's choice becomes (SPEC 4.6). Pure text, no GStreamer.
#pragma once

#include <string>
#include <vector>

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

}  // namespace gps
