// templates.h – template instances inside a Graph: placing a library template, membership, parameters, the template an
// instance would save to the library, and the fingerprint that tells whether its content changed since the last sync.
// Pure data like graph.h: no Qt, no GStreamer.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "graph.h"

namespace gps::templates {

// ---- membership
std::vector<std::string> members(const Graph& g, const std::string& id);   // every element inside, nested ones too
std::vector<std::string> nested(const Graph& g, const std::string& id);    // every instance inside, at any depth
std::vector<std::string> chain(const Graph& g, const std::string& element);  // instances around it, innermost first
bool inside(const Graph& g, const std::string& element, const std::string& id);

// ---- placing
std::string newId(const Graph& g, const std::string& templateName);  // "<name>#<n>", not used in g
// Copies the clip's elements, links and instances into g, moved by (dx, dy). Instances get new ids, and parent is the
// instance the clip's top-level elements and instances join ("" = none). Returns clip name -> new name, for elements
// and instances alike.
std::map<std::string, std::string> insertCopy(Graph& g, const Graph& clip, double dx, double dy, const std::string& parent = "");
// A library template as a new instance at (dx, dy) relative to its own coordinates. Returns the instance id.
std::string insertTemplate(Graph& g, const Graph& tmpl, double dx, double dy, const std::string& parent = "");
// The elements become a new instance of templateName (they join it from the innermost instance they all share).
std::string wrap(Graph& g, const std::vector<std::string>& elements, const std::string& templateName,
                 const std::vector<ExposedParam>& params);
// The instance stops being one: its members and nested instances move up to its parent.
void detach(Graph& g, const std::string& id);

// ---- the library side
// The instance as a library template: members under their names inside the template, its parameters as exposed.
Graph extract(const Graph& g, const std::string& id);
std::string fingerprint(const Graph& g, const std::string& id);  // of extract(), parameter values and positions left out
bool modified(const Graph& g, const std::string& id);            // content changed since the last sync
// After a sanctioned change (insert, update, save to the library): the instance and every one around it are in sync.
void sync(Graph& g, const std::string& id, int revision);
// The instance takes the content of a newer library template: its members are replaced, links from outside reach the
// same pads again (by the names inside the template), and parameters keep the values they had. False when it is gone.
bool replaceContent(Graph& g, const std::string& id, const Graph& tmpl);

// ---- parameters
std::string paramValue(const Graph& g, const ExposedParam& p);  // "" when not set (the element's default)
bool setParam(Graph& g, const ExposedParam& p, const std::string& value);
// Name a parameter gets by default: "<element as the user reads it>.<property>".
std::string paramName(const Graph& g, const ExposedParam& p);

}  // namespace gps::templates
