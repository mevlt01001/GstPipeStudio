// inspector.h – what the GStreamer registry knows about elements, as the plain structs of catalog.h (SPEC 7, gps-inspect).
// Everything here comes from the registry, pad templates and GParamSpec. No element is known by name.
#pragma once

#include <string>
#include <vector>

#include <gst/gst.h>

#include "catalog.h"

namespace gps {

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

// gps-inspect version: GStreamer numbers from gst_version(), the nvidia flag count and the registry counts.
VersionInfo versionInfo(const std::vector<FactoryEntry>& entries);

}  // namespace gps
