// capstext.cpp – see capstext.h and SPEC 4.6.
#include "capstext.h"

#include <algorithm>
#include <utility>

namespace gps {

std::string capsString(const std::string& value) {
    static const char bare[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_+-/:.";
    if (!value.empty() && value.find_first_not_of(bare) == std::string::npos) return value;

    std::string out = "\"";
    for (char c : value) {
        // GStreamer 1.16 cannot read a space inside quotes unless it has a backslash; newer versions accept both.
        if (c == '"' || c == '\\' || c == ' ') out += '\\';
        out += c;
    }
    return out + "\"";
}

namespace {

// Without its type GStreamer guesses: the string "3" becomes an int. For 64-bit numbers the caps parser
// reads only the GType names, "(int64)5" is refused and "(gint64)5" is not. One table, read both ways.
const std::vector<std::pair<std::string, std::string>>& typeNames() {
    static const std::vector<std::pair<std::string, std::string>> names = {
        {"gchararray", "string"}, {"gint", "int"},         {"guint", "uint"},
        {"gint64", "gint64"},     {"guint64", "guint64"},  {"gfloat", "float"},
        {"gdouble", "double"},    {"gboolean", "boolean"}, {"GstFraction", "fraction"},
        {"GstBitmask", "bitmask"},
    };
    return names;
}

std::string typePrefix(const std::string& type) {
    for (const auto& [gtype, written] : typeNames()) {
        if (gtype == type) return "(" + written + ")";
    }
    return "";
}

std::string gtypeOf(const std::string& written) {
    for (const auto& [gtype, name] : typeNames()) {
        if (name == written) return gtype;
    }
    return written;
}

std::string trimmed(const std::string& text) {
    size_t first = text.find_first_not_of(" \t"), last = text.find_last_not_of(" \t");
    return first == std::string::npos ? "" : text.substr(first, last - first + 1);
}

// Splits at commas outside quotes and parentheses.
std::vector<std::string> splitTopLevel(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quoted && c == '\\' && i + 1 < text.size()) {
            current += c;
            current += text[++i];
            continue;
        }
        if (c == '"') quoted = !quoted;
        if (!quoted && c == '(') ++depth;
        if (!quoted && c == ')') --depth;
        if (!quoted && depth == 0 && c == ',') {
            parts.push_back(trimmed(current));
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(trimmed(current));
    return parts;
}

// The inverse of capsString().
std::string unquote(const std::string& value) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return value;
    std::string out;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size()) ++i;
        out += value[i];
    }
    return out;
}

}  // namespace

std::string CapsChoice::toText() const {
    if (media.empty()) return "";

    std::string text = media;
    if (!features.empty()) {
        text += "(";
        for (size_t i = 0; i < features.size(); ++i) text += (i ? ", " : "") + features[i];
        text += ")";
    }
    for (const CapsField& f : fields) {
        text += ", " + f.name + "=" + typePrefix(f.type) + (f.type == "gchararray" ? capsString(f.value) : f.value);
    }
    return text;
}

CapsChoice CapsChoice::fromText(const std::string& text) {
    CapsChoice choice;
    std::vector<std::string> parts = splitTopLevel(text);
    if (parts.empty() || parts[0].empty()) return choice;

    const std::string& head = parts[0];
    size_t open = head.find('(');
    choice.media = trimmed(head.substr(0, open));
    if (open != std::string::npos && head.back() == ')') {
        std::string features = head.substr(open + 1, head.size() - open - 2);
        for (size_t start = 0, comma;; start = comma + 1) {
            comma = features.find(',', start);
            choice.features.push_back(trimmed(features.substr(start, comma - start)));
            if (comma == std::string::npos) break;
        }
    }
    for (size_t i = 1; i < parts.size(); ++i) {
        size_t eq = parts[i].find('=');
        if (eq == std::string::npos) continue;
        CapsField field;
        field.name = trimmed(parts[i].substr(0, eq));
        std::string rest = trimmed(parts[i].substr(eq + 1));
        field.type = "gchararray";
        size_t close = rest.find(')');
        if (!rest.empty() && rest[0] == '(' && close != std::string::npos) {
            field.type = gtypeOf(rest.substr(1, close - 1));
            rest = rest.substr(close + 1);
        }
        field.value = field.type == "gchararray" ? unquote(rest) : rest;
        choice.fields.push_back(field);
    }
    return choice;
}

std::string structureTitle(const StructureInfo& s) {
    std::string title = s.media;
    if (s.features.empty()) return title;
    title += "(";
    for (size_t i = 0; i < s.features.size(); ++i) title += (i ? ", " : "") + s.features[i];
    return title + ")";
}

std::string mediaOf(const std::string& caps) { return trimmed(caps.substr(0, caps.find(','))); }

bool structuresMeet(const std::vector<StructureInfo>& a, const std::vector<StructureInfo>& b) {
    if (a.empty() || b.empty()) return true;
    for (const StructureInfo& x : a) {
        for (const StructureInfo& y : b) {
            std::vector<std::string> fx = x.features, fy = y.features;
            std::sort(fx.begin(), fx.end());
            std::sort(fy.begin(), fy.end());
            if (x.media == y.media && fx == fy) return true;
        }
    }
    return false;
}

}  // namespace gps
