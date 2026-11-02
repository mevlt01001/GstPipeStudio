// capstext.cpp – see capstext.h and SPEC 4.6.
#include "capstext.h"

#include <map>

namespace gps {

std::string capsString(const std::string& value) {
    static const char bare[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_+-/:.";
    if (!value.empty() && value.find_first_not_of(bare) == std::string::npos) return value;

    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
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

}  // namespace gps
