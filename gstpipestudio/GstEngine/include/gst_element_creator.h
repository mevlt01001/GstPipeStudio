// gst_element_creator.h – Single-Responsibility class that instantiates GStreamer elements,
// validates & applies properties, resolves Always/Request/Sometimes pads, and wraps a Graph as a GstBin element.
#pragma once

#include <functional>
#include <map>
#include <string>

#include <gst/gst.h>

#include "graph.h"

namespace gps {

using LogFn = std::function<void(const std::string& line)>;

class GstElementCreator {
public:
    // Validates and sets a GObject/GstPad property string against its GParamSpec.
    static bool setProperty(GObject* object, const std::string& property, const std::string& text, std::string* reason);

    // Format helper: "<label>: '<text>' is not valid, <reason>"
    static std::string formatPropertyError(const std::string& label, const std::string& text, const std::string& reason);

    // Instantiates a GstElement from Element (factory + name + properties).
    // Returns nullptr and sets *error on failure.
    static GstElement* createElement(const Element& spec, std::string* error, const LogFn& log = {});

    // Requests a pad from an element by template name ("src_%u", "sink_%u"), trying numbered slots 0..63 if needed.
    static GstPad* requestPad(GstElement* element, const std::string& templateName, const LogFn& log = {});

    // Queries the GStreamer registry to determine FactoryTraits (e.g. whether factory is a Source).
    static FactoryTraits queryFactoryTraits(const std::string& factory);
};

}  // namespace gps

