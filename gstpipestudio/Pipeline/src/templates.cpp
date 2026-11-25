// templates.cpp – see templates.h.
#include "templates.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>

#include "capstext.h"

namespace gps::templates {
namespace {

// The instance id an element or an instance sits in, one level up.
std::string around(const Graph& g, const std::string& elementOrInstance) {
    if (const Element* e = g.find(elementOrInstance)) return e->group;
    const TemplateInstance* t = g.instance(elementOrInstance);
    return t ? t->parent : "";
}

bool within(const Graph& g, const std::string& elementOrInstance, const std::string& id) {
    for (std::string up = around(g, elementOrInstance); !up.empty(); up = around(g, up)) {
        if (up == id) return true;
    }
    return false;
}

// Where a caps field parameter lives: "caps.width" -> the element's own caps property when it has one, else the caps
// of its only src link; "src_0.caps.width" -> the caps of that src pad's link.
struct CapsTarget {
    std::string pad, field;
};
bool capsTarget(const std::string& property, CapsTarget* out) {
    const std::string marker = "caps.";
    size_t at = property.rfind(marker);
    if (at == std::string::npos || (at != 0 && property[at - 1] != '.')) return false;
    out->pad = at == 0 ? "" : property.substr(0, at - 1);
    out->field = property.substr(at + marker.size());
    return !out->field.empty();
}

// The caps text a caps field parameter reads and writes: the element's caps property or a link's caps choice.
std::string* capsText(Graph& g, const ExposedParam& p, const CapsTarget& target) {
    Element* e = g.find(p.element);
    if (!e) return nullptr;
    if (target.pad.empty()) {
        auto own = e->props.find("caps");
        if (own != e->props.end()) return &own->second;
        std::vector<int> out = g.linksFromElement(p.element);
        return out.size() == 1 ? &g.links[static_cast<size_t>(out.front())].caps : nullptr;
    }
    int link = g.linkFrom(p.element, target.pad);
    return link < 0 ? nullptr : &g.links[static_cast<size_t>(link)].caps;
}

std::string hash(const std::string& text) {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a, 64 bit
    for (unsigned char c : text) h = (h ^ c) * 1099511628211ULL;
    char out[17];
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
    return out;
}

ExposedParam renamed(ExposedParam p, const std::map<std::string, std::string>& names) {
    auto found = names.find(p.element);
    p.element = found == names.end() ? "" : found->second;
    return p;
}

}  // namespace

// ---- membership

std::vector<std::string> members(const Graph& g, const std::string& id) {
    std::vector<std::string> out;
    for (const Element& e : g.elements) {
        if (within(g, e.name, id)) out.push_back(e.name);
    }
    return out;
}

std::vector<std::string> nested(const Graph& g, const std::string& id) {
    std::vector<std::string> out;
    for (const TemplateInstance& t : g.instances) {
        if (within(g, t.id, id)) out.push_back(t.id);
    }
    return out;
}

std::vector<std::string> chain(const Graph& g, const std::string& element) {
    std::vector<std::string> out;
    for (std::string up = around(g, element); !up.empty(); up = around(g, up)) out.push_back(up);
    return out;
}

bool inside(const Graph& g, const std::string& element, const std::string& id) { return within(g, element, id); }

// ---- placing

std::string newId(const Graph& g, const std::string& templateName) {
    std::string base = templateName;
    std::replace(base.begin(), base.end(), '#', '_');
    for (int n = 1;; ++n) {
        std::string id = base + "#" + std::to_string(n);
        if (!g.instance(id)) return id;
    }
}

std::map<std::string, std::string> insertCopy(Graph& g, const Graph& clip, double dx, double dy, const std::string& parent) {
    std::map<std::string, std::string> names;
    for (const TemplateInstance& t : clip.instances) {
        TemplateInstance copy = t;
        copy.id = newId(g, t.templateName);
        names[t.id] = copy.id;
        g.instances.push_back(copy);
    }
    for (const Element& e : clip.elements) {
        const std::string created = g.addElement(e.factory, e.props, e.x + dx, e.y + dy);
        Element* added = g.find(created);
        added->requestSrc = e.requestSrc;
        added->requestSink = e.requestSink;
        auto group = names.find(e.group);
        added->group = group == names.end() ? parent : group->second;
        names[e.name] = created;
    }
    for (const Connection& l : clip.links) {
        auto src = names.find(l.srcElement), sink = names.find(l.sinkElement);
        if (src != names.end() && sink != names.end()) g.linkExisting(src->second, l.srcPad, sink->second, l.sinkPad, l.caps, l.sinkPadProps);
    }
    for (const TemplateInstance& t : clip.instances) {
        TemplateInstance* copy = g.instance(names[t.id]);
        auto up = names.find(t.parent);
        copy->parent = up == names.end() ? parent : up->second;
        copy->params.clear();
        for (const ExposedParam& p : t.params) {
            ExposedParam moved = renamed(p, names);
            if (!moved.element.empty()) copy->params.push_back(moved);
        }
        // The names inside the template stay what they were, so the fingerprint does not change with the copy.
        copy->local.clear();
        std::vector<std::string> inner = members(clip, t.id), innerInstances = nested(clip, t.id);
        inner.insert(inner.end(), innerInstances.begin(), innerInstances.end());
        for (const std::string& old : inner) {
            auto known = t.local.find(old);
            copy->local[names[old]] = known == t.local.end() ? old : known->second;
        }
    }
    return names;
}

std::string insertTemplate(Graph& g, const Graph& tmpl, double dx, double dy, const std::string& parent) {
    TemplateInstance t;
    t.id = newId(g, tmpl.name);
    t.templateName = tmpl.name;
    t.revision = tmpl.revision;
    t.parent = parent;
    const std::string id = t.id;
    g.instances.push_back(t);
    const std::map<std::string, std::string> names = insertCopy(g, tmpl, dx, dy, id);
    TemplateInstance* placed = g.instance(id);
    for (const ExposedParam& p : tmpl.exposed) {
        ExposedParam moved = renamed(p, names);
        if (!moved.element.empty()) placed->params.push_back(moved);
    }
    for (const auto& [old, created] : names) placed->local[created] = old;
    placed->signature = fingerprint(g, id);
    return id;
}

std::string wrap(Graph& g, const std::vector<std::string>& elements, const std::string& templateName,
                 const std::vector<ExposedParam>& params) {
    if (elements.empty()) return "";
    // The innermost instance every element sits in: the new one goes inside it.
    std::vector<std::string> shared = chain(g, elements.front());
    for (const std::string& e : elements) {
        const std::vector<std::string> mine = chain(g, e);
        shared.erase(std::remove_if(shared.begin(), shared.end(),
                                    [&](const std::string& id) { return std::find(mine.begin(), mine.end(), id) == mine.end(); }),
                     shared.end());
    }
    const std::string common = shared.empty() ? "" : shared.front();
    TemplateInstance t;
    t.id = newId(g, templateName);
    t.templateName = templateName;
    t.revision = 1;
    t.parent = common;
    t.params = params;
    const std::string id = t.id;
    g.instances.push_back(t);
    for (const std::string& name : elements) {
        // The outermost instance below common around the element moves in whole; a plain element moves in itself.
        std::string top = name;
        for (const std::string& up : chain(g, name)) {
            if (up == common || up == id) break;
            top = up;
        }
        if (Element* e = g.find(top)) e->group = id;
        else if (TemplateInstance* inner = g.instance(top)) inner->parent = id;
    }
    TemplateInstance* made = g.instance(id);
    for (const std::string& m : members(g, id)) made->local[m] = m;
    for (const std::string& n : nested(g, id)) made->local[n] = n;
    made->signature = fingerprint(g, id);
    return id;
}

void detach(Graph& g, const std::string& id) {
    const TemplateInstance* t = g.instance(id);
    if (!t) return;
    const std::string parent = t->parent;
    for (Element& e : g.elements) {
        if (e.group == id) e.group = parent;
    }
    for (TemplateInstance& other : g.instances) {
        if (other.parent == id) other.parent = parent;
        other.local.erase(id);
    }
    g.instances.erase(std::remove_if(g.instances.begin(), g.instances.end(), [&](const TemplateInstance& x) { return x.id == id; }),
                      g.instances.end());
}

// ---- references

std::string fileName(const std::string& templateName) {
    std::string safe = templateName;
    for (char& c : safe) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != '-') c = '_';
    }
    return safe + ".conf";
}

std::map<std::string, std::string> placeReference(Graph& g, const std::string& id, const Graph& tmpl,
                                                  const std::map<std::string, std::string>& values) {
    const std::string placed = insertTemplate(g, tmpl, 0, 0);
    for (Element& e : g.elements) {
        if (e.group == placed) e.group = id;
    }
    for (TemplateInstance& t : g.instances) {
        if (t.parent == placed) t.parent = id;
        if (t.id == placed) t.id = id;
    }
    TemplateInstance& t = *g.instance(id);
    for (const ExposedParam& p : std::vector<ExposedParam>(t.params)) {
        auto value = values.find(p.name);
        if (value != values.end()) setParam(g, p, value->second);
    }
    std::map<std::string, std::string> byInside;
    for (const auto& [now, inside] : g.instance(id)->local) byInside[inside] = now;
    return byInside;
}

// ---- the library side

Graph extract(const Graph& g, const std::string& id) {
    Graph out;
    const TemplateInstance* t = g.instance(id);
    if (!t) return out;
    out.name = t->templateName;
    out.revision = t->revision;
    out.nextId = g.nextId;
    const std::vector<std::string> inner = members(g, id), innerInstances = nested(g, id);
    const std::set<std::string> isMember(inner.begin(), inner.end());

    std::map<std::string, std::string> names;
    std::set<std::string> taken;
    auto pick = [&](const std::string& now) {
        auto known = t->local.find(now);
        const std::string base = known == t->local.end() ? now : known->second;
        std::string name = base;
        for (int k = 2; taken.count(name); ++k) name = base + "_" + std::to_string(k);
        taken.insert(name);
        names[now] = name;
    };
    for (const Element& e : g.elements) {
        if (isMember.count(e.name)) pick(e.name);
    }
    for (const std::string& n : innerInstances) pick(n);

    for (const Element& e : g.elements) {
        if (!isMember.count(e.name)) continue;
        Element copy = e;
        copy.name = names[e.name];
        copy.group = e.group == id ? "" : names[e.group];
        copy.pads.clear();
        out.elements.push_back(copy);
    }
    for (const Connection& l : g.links) {
        if (!isMember.count(l.srcElement) || !isMember.count(l.sinkElement)) continue;
        Connection copy = l;
        copy.srcElement = names[l.srcElement];
        copy.sinkElement = names[l.sinkElement];
        out.links.push_back(copy);
    }
    for (const std::string& n : innerInstances) {
        TemplateInstance copy = *g.instance(n);
        copy.id = names[n];
        copy.parent = copy.parent == id ? "" : names[copy.parent];
        for (ExposedParam& p : copy.params) p = renamed(p, names);
        std::map<std::string, std::string> local;
        for (const auto& [now, inside] : copy.local) {
            auto found = names.find(now);
            if (found != names.end()) local[found->second] = inside;
        }
        copy.local = local;
        out.instances.push_back(copy);
    }
    for (const ExposedParam& p : t->params) out.exposed.push_back(renamed(p, names));
    return out;
}

std::string fingerprint(const Graph& g, const std::string& id) {
    Graph content = extract(g, id);
    for (const ExposedParam& p : content.exposed) setParam(content, p, "");
    content.name.clear();
    content.revision = 0;
    content.nextId = 0;
    for (Element& e : content.elements) e.x = e.y = 0;
    // A nested template is held by reference: its own sync state is not part of this content.
    for (TemplateInstance& t : content.instances) t.revision = 0, t.signature.clear();
    return hash(content.toConf());
}

bool modified(const Graph& g, const std::string& id) {
    const TemplateInstance* t = g.instance(id);
    return t && fingerprint(g, id) != t->signature;
}

void sync(Graph& g, const std::string& id, int revision) {
    if (TemplateInstance* t = g.instance(id)) t->revision = revision;
    for (std::string up = id; !up.empty(); up = around(g, up)) {
        if (TemplateInstance* t = g.instance(up)) t->signature = fingerprint(g, up);
    }
}

bool replaceContent(Graph& g, const std::string& id, const Graph& tmpl) {
    const TemplateInstance* found = g.instance(id);
    if (!found) return false;
    const TemplateInstance before = *found;
    const std::vector<std::string> old = members(g, id);
    const std::set<std::string> inside(old.begin(), old.end());
    // What reaches into it from outside, by the names inside the template, and the values of its parameters.
    struct Crossing {
        Connection link;
        bool srcInside;
    };
    std::vector<Crossing> crossings;
    for (const Connection& l : g.links) {
        const bool src = inside.count(l.srcElement) > 0, sink = inside.count(l.sinkElement) > 0;
        if (src == sink) continue;
        Crossing c{l, src};
        std::string& end = src ? c.link.srcElement : c.link.sinkElement;
        auto local = before.local.find(end);
        end = local == before.local.end() ? end : local->second;
        crossings.push_back(c);
    }
    std::map<std::string, std::string> values;
    for (const ExposedParam& p : before.params) values[p.name] = paramValue(g, p);
    double x = 0, y = 0;
    if (!old.empty()) x = g.find(old.front())->x, y = g.find(old.front())->y;

    g.deleteElements(old, false);  // takes the instance and the ones inside it along
    TemplateInstance renewed;
    renewed.id = before.id;
    renewed.templateName = tmpl.name;
    renewed.revision = tmpl.revision;
    renewed.parent = g.instance(before.parent) ? before.parent : "";
    g.instances.push_back(renewed);
    double left = 0, top = 0;
    if (!tmpl.elements.empty()) left = tmpl.elements.front().x, top = tmpl.elements.front().y;
    const std::map<std::string, std::string> names = insertCopy(g, tmpl, x - left, y - top, before.id);
    TemplateInstance* placed = g.instance(before.id);
    for (const auto& [inTemplate, created] : names) placed->local[created] = inTemplate;
    for (const ExposedParam& p : tmpl.exposed) {
        ExposedParam moved = renamed(p, names);
        if (!moved.element.empty()) placed->params.push_back(moved);
    }
    for (const Crossing& c : crossings) {
        Connection l = c.link;
        std::string& end = c.srcInside ? l.srcElement : l.sinkElement;
        auto created = names.find(end);
        if (created == names.end()) continue;  // the newer template has no such element: the link goes
        end = created->second;
        g.linkExisting(l.srcElement, l.srcPad, l.sinkElement, l.sinkPad, l.caps, l.sinkPadProps);
    }
    for (const ExposedParam& p : std::vector<ExposedParam>(placed->params)) {
        auto value = values.find(p.name);
        if (value != values.end()) setParam(g, p, value->second);
    }
    sync(g, before.id, tmpl.revision);
    return true;
}

// ---- parameters

std::string paramValue(const Graph& g, const ExposedParam& p) {
    CapsTarget target;
    if (capsTarget(p.property, &target)) {
        const std::string* text = capsText(const_cast<Graph&>(g), p, target);
        if (!text) return "";
        for (const CapsField& f : CapsChoice::fromText(*text).fields) {
            if (f.name == target.field) return f.value;
        }
        return "";
    }
    const Element* e = g.find(p.element);
    if (!e) return "";
    auto found = e->props.find(p.property);
    return found == e->props.end() ? "" : found->second;
}

bool setParam(Graph& g, const ExposedParam& p, const std::string& value) {
    CapsTarget target;
    if (capsTarget(p.property, &target)) {
        std::string* text = capsText(g, p, target);
        if (!text) return false;
        CapsChoice caps = CapsChoice::fromText(*text);
        auto field = std::find_if(caps.fields.begin(), caps.fields.end(), [&](const CapsField& f) { return f.name == target.field; });
        if (field == caps.fields.end()) {
            if (value.empty()) return true;
            caps.fields.push_back({target.field, "gchararray", value});
        } else if (value.empty()) {
            caps.fields.erase(field);
        } else {
            field->value = value;
        }
        *text = caps.toText();
        return true;
    }
    Element* e = g.find(p.element);
    if (!e) return false;
    if (value.empty()) e->props.erase(p.property);
    else e->props[p.property] = value;
    return true;
}

std::string paramName(const Graph& g, const ExposedParam& p) {
    const auto names = g.displayNames();
    const auto it = names.find(p.element);
    return (it == names.end() ? p.element : it->second) + "." + p.property;
}

}  // namespace gps::templates
