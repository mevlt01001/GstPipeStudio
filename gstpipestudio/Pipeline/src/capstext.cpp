// capstext.cpp – see capstext.h and SPEC 4.6.
#include "capstext.h"

#include <algorithm>
#include <map>

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
// reads only the GType names, "(int64)5" is refused and "(gint64)5" is not.
std::string typePrefix(const std::string& type) {
    static const std::map<std::string, std::string> names = {
        {"gchararray", "string"}, {"gint", "int"},         {"guint", "uint"},
        {"gint64", "gint64"},     {"guint64", "guint64"},  {"gfloat", "float"},
        {"gdouble", "double"},    {"gboolean", "boolean"}, {"GstFraction", "fraction"},
        {"GstBitmask", "bitmask"},
    };
    auto found = names.find(type);
    return found == names.end() ? "" : "(" + found->second + ")";
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

std::string structureTitle(const StructureInfo& s) {
    std::string title = s.media;
    if (s.features.empty()) return title;
    title += "(";
    for (size_t i = 0; i < s.features.size(); ++i) title += (i ? ", " : "") + s.features[i];
    return title + ")";
}

std::string mediaOf(const std::string& caps) {
    std::string head = caps.substr(0, caps.find(','));
    size_t first = head.find_first_not_of(" \t"), last = head.find_last_not_of(" \t");
    return first == std::string::npos ? "" : head.substr(first, last - first + 1);
}

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
