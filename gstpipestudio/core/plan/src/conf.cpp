// conf.cpp – the .conf text form of a Plan (SPEC 3). Provisional format, kept in this one file.
#include <algorithm>
#include <cctype>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>

#include "plan.h"

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

std::string number(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << value;
    return out.str();
}

bool parseNumber(const std::string& text, double* value) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    in >> *value;
    return !in.fail();
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

std::string Plan::toConf() const {
    std::ostringstream out;
    out << "[gstpipestudio]\nversion=1\n";
    put(out, "name", name);
    out << "complete=" << (complete ? 1 : 0) << "\nnext=" << nextId << "\n";

    for (const PlanElement& e : elements) {
        out << "\n[element:" << e.name << "]\n";
        put(out, "factory", e.factory);
        if (!e.group.empty()) put(out, "group", e.group);
        out << "x=" << number(e.x) << "\ny=" << number(e.y) << "\n";
        for (const auto& r : e.requestSrc) out << "request." << r.first << "=" << r.second << "\n";
        for (const auto& r : e.requestSink) out << "requestsink." << r.first << "=" << r.second << "\n";
        for (const auto& p : e.props) put(out, "prop." + p.first, p.second);
    }
    for (size_t i = 0; i < links.size(); ++i) {
        const PlanLink& l = links[i];
        out << "\n[link:" << i << "]\n";
        put(out, "src", l.srcElement);
        put(out, "srcpad", l.srcPad);
        put(out, "sink", l.sinkElement);
        put(out, "sinkpad", l.sinkPad);
        if (!l.caps.empty()) put(out, "caps", l.caps);
        for (const auto& p : l.sinkPadProps) put(out, "padprop." + p.first, p.second);
    }
    for (const ExposedParam& p : exposed) {
        out << "\n[expose:" << p.name << "]\n";
        put(out, "target", p.element + "." + p.property);
    }
    return out.str();
}

bool Plan::fromConf(const std::string& text, Plan* out, std::string* error) {
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
    Plan plan;
    plan.name = "pipeline";
    bool header = false;
    int next = 0;
    std::set<std::string> names;
    for (const Section& s : sections) {
        if (s.kind == "gstpipestudio") {
            header = true;
            const std::string* version = s.get("version");
            if (!version || *version != "1") return fail(error, s.line, "unsupported version");
            if (const std::string* v = s.get("name")) plan.name = *v;
            if (const std::string* v = s.get("complete")) plan.complete = *v == "1";
            if (const std::string* v = s.get("next")) next = std::max(0, std::atoi(v->c_str()));
        } else if (s.kind == "element") {
            if (!validName(s.name)) return fail(error, s.line, "bad element name '" + s.name + "'");
            if (!names.insert(s.name).second) return fail(error, s.line, "duplicate element '" + s.name + "'");
            const std::string* factory = s.get("factory");
            if (!factory || factory->empty()) return fail(error, s.line, "element without factory");
            PlanElement e;
            e.name = s.name;
            e.factory = *factory;
            for (const auto& kv : s.keys) {
                if (kv.first == "x") parseNumber(kv.second, &e.x);
                else if (kv.first == "y") parseNumber(kv.second, &e.y);
                else if (kv.first == "group") e.group = kv.second;
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
            PlanLink l;
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
        }
    }
    if (!header) return fail(error, 1, "missing [gstpipestudio] section");
    plan.nextId = next;

    // 3. The plan must make sense on its own.
    std::set<std::pair<std::string, std::string>> srcPads;
    std::set<std::pair<std::string, std::string>> sinkPads;
    for (const PlanLink& l : plan.links) {
        if (!names.count(l.srcElement) || !names.count(l.sinkElement)) {
            return fail(error, 0, "link " + l.srcElement + " -> " + l.sinkElement + " names an element that does not exist");
        }
        if (!srcPads.insert({l.srcElement, l.srcPad}).second) {
            return fail(error, 0, "src pad " + l.srcElement + "." + l.srcPad + " has two links");
        }
        if (!isTemplateName(l.sinkPad) && !sinkPads.insert({l.sinkElement, l.sinkPad}).second) {
            return fail(error, 0, "sink pad " + l.sinkElement + "." + l.sinkPad + " has two links");
        }
    }
    {
        Plan check;
        for (const PlanElement& e : plan.elements) check.elements.push_back(e);
        for (const PlanLink& l : plan.links) {
            if (check.wouldCycle(l.srcElement, l.sinkElement)) {
                return fail(error, 0, "link " + l.srcElement + " -> " + l.sinkElement + " makes a cycle");
            }
            check.links.push_back(l);
        }
    }
    for (const ExposedParam& p : plan.exposed) {
        if (!names.count(p.element)) return fail(error, 0, "expose '" + p.name + "' names an element that does not exist");
    }

    *out = plan;
    return true;
}

bool Plan::save(const std::string& path, std::string* error) const {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    file << toConf();
    file.flush();
    if (!file) {
        if (error) *error = "write failed for " + path;
        return false;
    }
    return true;
}

bool Plan::load(const std::string& path, Plan* out, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    return fromConf(text.str(), out, error);
}

}  // namespace gps
