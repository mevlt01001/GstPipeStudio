// conf.cpp – the .conf text form of a Graph (SPEC 3). Provisional format, kept in this one file.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "files.h"
#include "graph.h"
#include "templates.h"

namespace gps {
namespace {

std::string escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string unescape(const std::string& value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out += value[i];
            continue;
        }
        char next = value[++i];
        out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}

bool validName(const std::string& name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

struct Section {
    std::string kind, name;
    int line = 0;
    std::vector<std::pair<std::string, std::string>> keys;

    const std::string* get(const std::string& key) const {
        for (const auto& kv : keys) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

bool fail(std::string* error, int line, const std::string& message) {
    if (error) *error = "line " + std::to_string(line) + ": " + message;
    return false;
}

void put(std::ostringstream& out, const std::string& key, const std::string& value) {
    out << key << "=" << escape(value) << "\n";
}

// The number a name ends with, "avidemux_12" -> 12, or -1.
int suffixOf(const std::string& name) {
    size_t u = name.rfind('_');
    if (u == std::string::npos || u + 1 >= name.size()) return -1;
    for (size_t i = u + 1; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) return -1;
    }
    return name.size() - u > 9 ? -1 : std::stoi(name.substr(u + 1));
}

}  // namespace

std::vector<std::string> Graph::referencable() const {
    std::vector<std::string> out;
    for (const TemplateInstance& t : instances) {
        if (t.parent.empty() && !templates::modified(*this, t.id)) out.push_back(t.id);
    }
    return out;
}

std::string Graph::toConf(Form form) const {
    return writeConf(form, form == Form::Reference ? referencable() : std::vector<std::string>());
}

std::string Graph::writeConf(Form form, const std::vector<std::string>& copies) const {
    // In the reference form, the members of each copy referenced are written as the copy: "<copy>" for an element,
    // "<member inside the template>.<pad>" for a pad. Every other copy is written inline, so nothing is lost.
    std::map<std::string, std::string> owner;  // member element or nested instance -> the referenced copy around it
    std::vector<const TemplateInstance*> referenced;
    for (const std::string& id : copies) {
        const TemplateInstance* t = instance(id);
        if (!t) continue;
        referenced.push_back(t);
        for (const std::string& m : templates::members(*this, t->id)) owner[m] = t->id;
        for (const std::string& n : templates::nested(*this, t->id)) owner[n] = t->id;
    }
    auto end = [&](const std::string& element, const std::string& pad, std::string* outPad) {
        auto copy = owner.find(element);
        if (copy == owner.end()) {
            *outPad = pad;
            return element;
        }
        *outPad = instance(copy->second)->local.at(element) + "." + pad;
        return copy->second;
    };

    std::ostringstream out;
    out << "[gstpipestudio]\nversion=1\n";
    put(out, "name", name);
    out << "complete=" << (complete ? 1 : 0) << "\n";
    // The reader starts numbering after the highest number it reads; referenced members get new names every time.
    if (form == Form::Inline) out << "next=" << nextId << "\n";
    if (revision) out << "revision=" << revision << "\n";

    for (const Element& e : elements) {
        if (owner.count(e.name)) continue;
        out << "\n[element:" << e.name << "]\n";
        put(out, "factory", e.factory);
        if (!e.group.empty()) put(out, "group", e.group);
        for (const auto& r : e.requestSrc) out << "request." << r.first << "=" << r.second << "\n";
        for (const auto& r : e.requestSink) out << "requestsink." << r.first << "=" << r.second << "\n";
        for (const auto& p : e.props) put(out, "prop." + p.first, p.second);
    }
    int written = 0;
    for (const Connection& l : links) {
        auto from = owner.find(l.srcElement), to = owner.find(l.sinkElement);
        if (from != owner.end() && to != owner.end() && from->second == to->second) continue;  // inside one copy
        std::string srcPad, sinkPad;
        out << "\n[link:" << written++ << "]\n";
        put(out, "src", end(l.srcElement, l.srcPad, &srcPad));
        put(out, "srcpad", srcPad);
        put(out, "sink", end(l.sinkElement, l.sinkPad, &sinkPad));
        put(out, "sinkpad", sinkPad);
        if (!l.caps.empty()) put(out, "caps", l.caps);
        for (const auto& p : l.sinkPadProps) put(out, "padprop." + p.first, p.second);
    }
    for (const ExposedParam& p : exposed) {
        std::string property;
        const std::string element = end(p.element, p.property, &property);
        out << "\n[expose:" << p.name << "]\n";
        put(out, "target", element + "." + property);
    }
    for (const TemplateInstance* t : referenced) {
        out << "\n[template:" << t->id << "]\n";
        put(out, "template", t->templateName);
        out << "revision=" << t->revision << "\n";
        for (const ExposedParam& p : t->params) {
            const std::string value = templates::paramValue(*this, p);
            if (!value.empty()) put(out, "prop." + p.name, value);
        }
    }
    for (const TemplateInstance& t : instances) {
        if (owner.count(t.id) || std::any_of(referenced.begin(), referenced.end(), [&](const TemplateInstance* r) { return r->id == t.id; }))
            continue;
        out << "\n[template:" << t.id << "]\n";
        put(out, "template", t.templateName);
        out << "revision=" << t.revision << "\n";
        if (!t.parent.empty()) put(out, "parent", t.parent);
        if (!t.signature.empty()) put(out, "signature", t.signature);
        for (const auto& [member, inside] : t.local) put(out, "local." + member, inside);
        for (const ExposedParam& p : t.params) put(out, "param." + p.name, p.element + "." + p.property);
    }
    return out.str();
}

bool Graph::fromConf(const std::string& text, Graph* out, std::string* error, const TemplateSource& templates) {
    // 1. Cut the text into sections.
    std::vector<Section> sections;
    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.empty() || raw[0] == '#') continue;
        if (raw[0] == '[') {
            if (raw.back() != ']') return fail(error, lineNo, "unclosed section header");
            std::string head = raw.substr(1, raw.size() - 2);
            Section s;
            size_t colon = head.find(':');
            s.kind = head.substr(0, colon);
            if (colon != std::string::npos) s.name = head.substr(colon + 1);
            s.line = lineNo;
            sections.push_back(s);
            continue;
        }
        size_t eq = raw.find('=');
        if (eq == std::string::npos) return fail(error, lineNo, "expected key=value");
        if (sections.empty()) return fail(error, lineNo, "key before the first section");
        sections.back().keys.emplace_back(raw.substr(0, eq), unescape(raw.substr(eq + 1)));
    }

    // 2. Read them in order. Unknown sections and keys are ignored.
    Graph plan;
    plan.name = "pipeline";
    std::map<std::string, std::map<std::string, std::string>> values;  // referenced copy -> parameter values
    bool header = false;
    int next = 0;
    for (const Section& s : sections) {
        if (s.kind == "gstpipestudio") {
            header = true;
            const std::string* version = s.get("version");
            if (!version || *version != "1") return fail(error, s.line, "unsupported version");
            if (const std::string* v = s.get("name")) plan.name = *v;
            if (const std::string* v = s.get("complete")) plan.complete = *v == "1";
            if (const std::string* v = s.get("next")) next = std::max(0, std::atoi(v->c_str()));
            if (const std::string* v = s.get("revision")) plan.revision = std::max(0, std::atoi(v->c_str()));
        } else if (s.kind == "element") {
            if (!validName(s.name)) return fail(error, s.line, "bad element name '" + s.name + "'");
            const std::string* factory = s.get("factory");
            if (!factory || factory->empty()) return fail(error, s.line, "element without factory");
            Element e;
            e.name = s.name;
            e.factory = *factory;
            for (const auto& kv : s.keys) {
                // x and y of files before 0.4 are left out: nice alignment places every box when the file opens.
                if (kv.first == "group") e.group = kv.second;
                else if (kv.first.rfind("prop.", 0) == 0) e.props[kv.first.substr(5)] = kv.second;
                else if (kv.first.rfind("request.", 0) == 0) e.requestSrc[kv.first.substr(8)] = std::max(0, std::atoi(kv.second.c_str()));
                else if (kv.first.rfind("requestsink.", 0) == 0) e.requestSink[kv.first.substr(12)] = std::max(0, std::atoi(kv.second.c_str()));
            }
            plan.elements.push_back(e);
            next = std::max(next, suffixOf(e.name) + 1);
        } else if (s.kind == "link") {
            const std::string* src = s.get("src");
            const std::string* srcPad = s.get("srcpad");
            const std::string* sink = s.get("sink");
            const std::string* sinkPad = s.get("sinkpad");
            if (!src || !srcPad || !sink || !sinkPad) return fail(error, s.line, "link needs src, srcpad, sink and sinkpad");
            Connection l;
            l.srcElement = *src;
            l.srcPad = *srcPad;
            l.sinkElement = *sink;
            l.sinkPad = *sinkPad;
            if (const std::string* caps = s.get("caps")) l.caps = *caps;
            for (const auto& kv : s.keys) {
                if (kv.first.rfind("padprop.", 0) == 0) l.sinkPadProps[kv.first.substr(8)] = kv.second;
            }
            plan.links.push_back(l);
        } else if (s.kind == "expose") {
            const std::string* target = s.get("target");
            size_t dot = target ? target->find('.') : std::string::npos;
            if (dot == std::string::npos || s.name.empty()) return fail(error, s.line, "expose needs target=element.property");
            plan.exposed.push_back({s.name, target->substr(0, dot), target->substr(dot + 1)});
        } else if (s.kind == "template") {
            if (s.name.empty() || plan.instance(s.name)) return fail(error, s.line, "template section needs a unique id");
            TemplateInstance t;
            t.id = s.name;
            for (const auto& kv : s.keys) {
                if (kv.first.rfind("prop.", 0) == 0) values[t.id][kv.first.substr(5)] = kv.second;  // a reference's parameters
                else if (kv.first == "template") t.templateName = kv.second;
                else if (kv.first == "revision") t.revision = std::max(0, std::atoi(kv.second.c_str()));
                else if (kv.first == "parent") t.parent = kv.second;
                else if (kv.first == "signature") t.signature = kv.second;
                else if (kv.first.rfind("local.", 0) == 0) t.local[kv.first.substr(6)] = kv.second;
                else if (kv.first.rfind("param.", 0) == 0) {
                    size_t dot = kv.second.find('.');
                    if (dot == std::string::npos) return fail(error, s.line, "param needs element.property");
                    t.params.push_back({kv.first.substr(6), kv.second.substr(0, dot), kv.second.substr(dot + 1)});
                }
            }
            if (t.templateName.empty()) t.templateName = t.id.substr(0, t.id.find('#'));
            plan.instances.push_back(t);
        }
    }
    // Files before 0.4 tag group members only: every group becomes an instance of the template it was named after.
    for (const Element& e : plan.elements) {
        if (e.group.empty() || plan.instance(e.group)) continue;
        TemplateInstance t;
        t.id = e.group;
        t.templateName = e.group.substr(0, e.group.find('#'));
        plan.instances.push_back(t);
    }
    if (!header) return fail(error, 1, "missing [gstpipestudio] section");
    plan.nextId = next;

    // A copy that holds nothing in the file is a reference: its template's content comes from its own file, and the
    // links and exposes that name "<copy>" and "<member inside it>.<pad>" reach the members it placed.
    std::map<std::string, std::map<std::string, std::string>> placed;  // copy -> member inside the template -> name
    for (const TemplateInstance& t : std::vector<TemplateInstance>(plan.instances)) {
        const bool holds = std::any_of(plan.elements.begin(), plan.elements.end(), [&](const Element& e) { return e.group == t.id; }) ||
                           std::any_of(plan.instances.begin(), plan.instances.end(), [&](const TemplateInstance& i) { return i.parent == t.id; });
        if (holds || !t.parent.empty()) continue;
        Graph tmpl;
        std::string why = "no template folder to look in";
        if (!templates || !templates(t.templateName, &tmpl, &why)) return fail(error, 0, "template " + t.templateName + ": " + why);
        plan.instances.erase(std::remove_if(plan.instances.begin(), plan.instances.end(), [&](const TemplateInstance& i) { return i.id == t.id; }),
                             plan.instances.end());
        placed[t.id] = templates::placeReference(plan, t.id, tmpl, values[t.id]);
    }
    auto reach = [&](std::string* element, std::string* pad) {
        auto copy = placed.find(*element);
        if (copy == placed.end()) return true;
        const size_t dot = pad->find('.');
        auto member = dot == std::string::npos ? copy->second.end() : copy->second.find(pad->substr(0, dot));
        if (member == copy->second.end()) return false;
        *element = member->second;
        *pad = pad->substr(dot + 1);
        return true;
    };
    for (Connection& l : plan.links) {
        if (!reach(&l.srcElement, &l.srcPad) || !reach(&l.sinkElement, &l.sinkPad))
            return fail(error, 0, "link " + l.srcElement + " -> " + l.sinkElement + " names a member its template does not have");
    }
    for (ExposedParam& p : plan.exposed) {
        if (!reach(&p.element, &p.property)) return fail(error, 0, "expose '" + p.name + "' names a member its template does not have");
    }
    std::string why;
    if (!plan.validate(&why)) return fail(error, 0, why);
    // An instance read without a fingerprint (older files) starts in sync with what it holds now.
    for (TemplateInstance& t : plan.instances) {
        if (t.signature.empty()) t.signature = templates::fingerprint(plan, t.id);
    }

    *out = plan;
    return true;
}

namespace {

std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

}  // namespace

bool Graph::save(const std::string& path, std::string* error) const { return files::writeAtomic(path, toConf(), error); }

std::string Graph::templatesFolder(const std::string& confPath) { return folderOf(confPath) + "/" + kTemplatesFolder; }

// The revision of the template file there, 0 when there is none or it cannot be read.
int Graph::revisionOf(const std::string& templateFile) {
    std::ifstream file(templateFile, std::ios::binary);
    if (!file) return 0;
    std::stringstream text;
    text << file.rdbuf();
    Graph tmpl;
    return fromConf(text.str(), &tmpl, nullptr) ? tmpl.revision : 0;
}

// The confs of one folder share its templates/: a copy older than the file there (another conf took a newer revision)
// is written inline instead, so saving this conf never takes the other one back.
bool Graph::saveProject(const std::string& path, std::string* error) const {
    const std::string folder = templatesFolder(path);
    std::vector<std::string> referenced;
    for (const std::string& id : referencable()) {
        const TemplateInstance* t = instance(id);
        const std::string file = folder + "/" + templates::fileName(t->templateName);
        if (revisionOf(file) > t->revision) continue;
        if (!files::writeAtomic(file, templates::extract(*this, id).toConf(), error)) return false;
        referenced.push_back(id);
    }
    return files::writeAtomic(path, writeConf(Form::Reference, referenced), error);
}

bool Graph::load(const std::string& path, Graph* out, std::string* error, const std::vector<std::string>& libraries) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    std::vector<std::string> folders{templatesFolder(path)};
    folders.insert(folders.end(), libraries.begin(), libraries.end());
    if (const char* more = std::getenv("GPS_TEMPLATE_PATH")) {
        std::stringstream list(more);
        for (std::string dir; std::getline(list, dir, ':');) {
            if (!dir.empty()) folders.push_back(dir);
        }
    }
    auto find = [&](const std::string& name, Graph* tmpl, std::string* why) {
        for (const std::string& dir : folders) {
            const std::string candidate = dir + "/" + templates::fileName(name);
            if (std::ifstream(candidate).good()) return load(candidate, tmpl, why, libraries);
        }
        std::string looked;
        for (const std::string& dir : folders) looked += (looked.empty() ? "" : ", ") + dir;
        *why = "not found in " + looked;
        return false;
    };
    return fromConf(text.str(), out, error, find);
}

}  // namespace gps
