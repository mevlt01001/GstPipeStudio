// names.cpp – see names.h.
#include "names.h"

namespace gps {

namespace {
// "video_%u#3" -> "video_3": the number takes the template's slot.
std::string filled(const std::string& identity) {
    std::string templateName;
    int k = 0;
    if (!splitPadIdentity(identity, &templateName, &k)) return identity;
    for (const char* slot : {"%u", "%d"}) {
        const size_t at = templateName.find(slot);
        if (at != std::string::npos) templateName.replace(at, 2, std::to_string(k));
    }
    return templateName;
}

std::string lookup(const std::map<std::string, std::string>& table, const std::string& key) {
    const auto found = table.find(key);
    return found == table.end() ? key : found->second;
}
}  // namespace

Names::Names() : table_(std::make_shared<Table>()) {}

Names::Names(const Graph& graph) {
    auto table = std::make_shared<Table>();
    std::map<std::string, int> seen;
    for (const Element& e : graph.elements) {
        const int n = ++seen[e.factory];
        table->elements[e.name] = n == 1 ? e.factory : e.factory + " " + std::to_string(n);
    }
    for (const TemplateInstance& t : graph.instances) table->instances[t.id] = t.templateName.empty() ? t.id : t.templateName;
    table_ = std::move(table);
}

std::string Names::element(const std::string& name) const { return lookup(table_->elements, name); }

std::string Names::label(const Pad& pad) { return pad.nativeName.empty() ? filled(pad.identity) : pad.nativeName; }

std::string Names::pad(const Graph& graph, const PadRef& ref) const {
    std::string text = filled(ref.identity);
    if (const Element* e = graph.find(ref.element)) {
        for (const Pad& p : e->pads) {
            if (p.identity == ref.identity && p.direction == ref.direction) text = label(p);
        }
    }
    return element(ref.element) + "." + text;
}

std::string Names::instance(const std::string& id) const { return lookup(table_->instances, id); }

std::string Names::param(const ExposedParam& p) const { return element(p.element) + "." + p.property; }

}  // namespace gps
