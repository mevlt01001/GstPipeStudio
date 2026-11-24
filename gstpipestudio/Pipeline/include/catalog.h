// catalog.h – what the GStreamer registry tells about element factories (SPEC 7), as plain data.
// GstEngine fills it from the registry; the GUI reads the same structs from gps-inspect through wire.h. No Qt, no GStreamer.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "graph.h"
#include "runtime.h"

namespace gps {

// One row of "gps-inspect list".
struct FactoryEntry {
    std::string factory, klass, longName, plugin;
    int rank = 0;
    bool isSource = false, isSink = false;
    bool hasAlwaysSrc = false, hasAlwaysSink = false;
    bool hasSometimesSrc = false, hasSometimesSink = false;
    bool hasRequestSrc = false, hasRequestSink = false;
    bool nvidia = false;

    bool isFilter() const { return !isSource && !isSink; }
};

struct EnumValue {
    std::string nick, name, value;
};

struct PropertyInfo {
    std::string name, nick, blurb, kind, typeName, defaultValue, min, max;
    std::vector<EnumValue> enumValues;
    bool readable = false, writable = false, construct = false, constructOnly = false, deprecated = false;
    bool mutableReady = false, mutablePaused = false, mutablePlaying = false;
};

struct PadTemplateInfo {
    std::string name, direction, presence, caps;  // direction: src|sink, presence: always|sometimes|request
    std::vector<StructureInfo> structures;
    std::vector<PropertyInfo> padProperties;

    bool isSrc() const { return direction == "src"; }
    bool isRequest() const { return presence == "request"; }
};

struct ElementInfo {
    FactoryEntry entry;
    std::string description, author;
    std::vector<PadTemplateInfo> padTemplates;
    std::vector<PropertyInfo> properties;

    std::vector<PadTemplateInfo> sinkTemplates() const;
    std::vector<PadTemplateInfo> srcTemplates() const;
    const PadTemplateInfo* padTemplate(const std::string& name, const std::string& direction) const;
    // Sink templates whose caps meet the given structures; all of them when none does (the user decides).
    std::vector<PadTemplateInfo> sinkTemplatesFor(const std::vector<StructureInfo>& allowed) const;
    std::map<std::string, int> defaultRequestSrc() const;
    std::map<std::string, int> defaultRequestSink() const;
};

// The factory's pad templates as the Pipeline's own factory definition (graph.h).
FactoryDefinition definitionOf(const ElementInfo& info);

// The answers of gps-inspect (SPEC 7).
struct FactoryList {
    std::vector<FactoryEntry> elements;
};
struct CompatibleFactories {
    std::vector<std::string> factories;
};
struct CapsResult {
    bool ok = false;
    std::string error, text;
    std::vector<StructureInfo> structures;
};
struct VersionInfo {
    struct Gst { std::string version; int major = 0, minor = 0, micro = 0; } gstreamer;
    struct Nvidia { bool present = false; int elements = 0; } nvidia;
    struct Plugins { int count = 0, elements = 0; } plugins;
};

}  // namespace gps
