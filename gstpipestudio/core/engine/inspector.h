// inspector.h – what the GStreamer registry knows about elements, as plain structs and JSON (SPEC 7, gps-inspect).
// Everything here comes from the registry, pad templates and GParamSpec. No element is known by name.
#pragma once

#include <string>
#include <vector>

#include <gst/gst.h>

#include "json.h"

namespace gps {

struct FactoryEntry {
    std::string factory, klass, longName, plugin;
    int rank = 0;
    bool isSource = false, isSink = false, hasRequestSrc = false, nvidia = false;
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

struct PadTemplateInfo {
    std::string name, direction, presence, caps;  // direction: src|sink, presence: always|sometimes|request
    std::vector<StructureInfo> structures;
    std::vector<PropertyInfo> padProperties;
};

struct ElementInfo {
    FactoryEntry entry;
    std::string description, author;
    std::vector<PadTemplateInfo> padTemplates;
    std::vector<PropertyInfo> properties;
};

// ---- the questions
std::vector<FactoryEntry> listFactories();
bool inspect(const std::string& factory, ElementInfo* out, std::string* error);
// Factories with a sink pad template that intersects the caps, best rank first, then by name.
// Empty caps text: every factory that has a sink template.
bool compatibleFactories(const std::string& capsText, std::vector<std::string>* out, std::string* error);
bool parseCaps(const std::string& text, std::string* normalized, std::vector<StructureInfo>* out, std::string* error);

// Structures of caps that GStreamer already parsed.
std::vector<StructureInfo> structuresOf(const GstCaps* caps);
// One property of a GObject class, as the GUI wants to see it.
PropertyInfo describeProperty(GParamSpec* spec);

// ---- JSON (SPEC 7)
void writeStructure(JsonWriter& w, const StructureInfo& s);
std::string listJson(const std::vector<FactoryEntry>& entries);
std::string elementJson(const ElementInfo& info);
std::string compatibleJson(const std::vector<std::string>& factories);
std::string capsJson(bool ok, const std::string& textOrError, const std::vector<StructureInfo>& structures);

}  // namespace gps
