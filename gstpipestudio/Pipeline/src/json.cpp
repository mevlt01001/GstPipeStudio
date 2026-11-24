// json.cpp – see json.h.
#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <locale>
#include <sstream>

namespace gps {
namespace {

// Length of the valid UTF-8 sequence at text[i], or 0 when the bytes are not valid UTF-8.
size_t utf8Length(const std::string& text, size_t i) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    size_t length = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    if (length == 0 || i + length > text.size()) return 0;
    for (size_t k = 1; k < length; ++k) {
        if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return 0;
    }
    unsigned char c1 = length > 1 ? static_cast<unsigned char>(text[i + 1]) : 0;
    if (c == 0xE0 && c1 < 0xA0) return 0;   // overlong
    if (c == 0xED && c1 > 0x9F) return 0;   // surrogate
    if (c == 0xF0 && c1 < 0x90) return 0;   // overlong
    if (c == 0xF4 && c1 > 0x8F) return 0;   // above U+10FFFF
    return length;
}

}  // namespace

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"') out += "\\\"", ++i;
        else if (c == '\\') out += "\\\\", ++i;
        else if (c == '\n') out += "\\n", ++i;
        else if (c == '\r') out += "\\r", ++i;
        else if (c == '\t') out += "\\t", ++i;
        else if (c == '\b') out += "\\b", ++i;
        else if (c == '\f') out += "\\f", ++i;
        else if (c < 0x20 || c == 0x7F) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
            ++i;
        } else {
            size_t length = utf8Length(text, i);
            if (length == 0) {
                out += "\xEF\xBF\xBD";  // U+FFFD
                ++i;
            } else {
                out.append(text, i, length);
                i += length;
            }
        }
    }
    return out;
}

void JsonWriter::beforeValue() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (first_.empty()) return;
    if (!first_.back()) out_ += ',';
    first_.back() = false;
}

JsonWriter& JsonWriter::beginObject() {
    beforeValue();
    out_ += '{';
    first_.push_back(true);
    kind_.push_back('{');
    return *this;
}

JsonWriter& JsonWriter::beginArray() {
    beforeValue();
    out_ += '[';
    first_.push_back(true);
    kind_.push_back('[');
    return *this;
}

JsonWriter& JsonWriter::end() {
    if (kind_.empty()) return *this;
    out_ += kind_.back() == '{' ? '}' : ']';
    kind_.pop_back();
    first_.pop_back();
    return *this;
}

JsonWriter& JsonWriter::key(const std::string& name) {
    beforeValue();
    out_ += '"' + jsonEscape(name) + "\":";
    afterKey_ = true;
    return *this;
}

JsonWriter& JsonWriter::string(const std::string& value) {
    beforeValue();
    out_ += '"' + jsonEscape(value) + '"';
    return *this;
}

JsonWriter& JsonWriter::number(double value) {
    beforeValue();
    if (!std::isfinite(value)) {
        out_ += "null";
        return *this;
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());  // "1.5" in every process locale, like jsonNumber() reads it
    text.precision(15);
    text << value;
    out_ += text.str();
    return *this;
}

JsonWriter& JsonWriter::integer(long long value) {
    beforeValue();
    out_ += std::to_string(value);
    return *this;
}

JsonWriter& JsonWriter::boolean(bool value) {
    beforeValue();
    out_ += value ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::null() {
    beforeValue();
    out_ += "null";
    return *this;
}

// ---- reading

double jsonNumber(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());  // a Qt program runs with the user's locale, where the decimal point may be a comma
    double value = 0;
    in >> value;
    return in.fail() ? 0 : value;
}

const JsonValue* JsonValue::find(const std::string& key) const {
    for (const auto& member : members) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool document(JsonValue* out) {
        if (!value(out, 0)) return false;
        space();
        return at_ == text_.size();
    }

private:
    static constexpr int kMaxDepth = 64;

    void space() {
        while (at_ < text_.size() && std::strchr(" \t\r\n", text_[at_])) ++at_;
    }
    bool take(char c) {
        space();
        if (at_ >= text_.size() || text_[at_] != c) return false;
        ++at_;
        return true;
    }
    bool word(const char* w) {
        size_t n = std::strlen(w);
        if (text_.compare(at_, n, w) != 0) return false;
        at_ += n;
        return true;
    }

    bool value(JsonValue* out, int depth) {
        if (depth > kMaxDepth) return false;
        space();
        if (at_ >= text_.size()) return false;
        char c = text_[at_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out->type = JsonValue::Type::String;
            return string(&out->text);
        }
        if (word("true")) { out->type = JsonValue::Type::Bool; out->boolean = true; return true; }
        if (word("false")) { out->type = JsonValue::Type::Bool; return true; }
        if (word("null")) { out->type = JsonValue::Type::Null; return true; }
        return number(out);
    }

    bool object(JsonValue* out, int depth) {
        out->type = JsonValue::Type::Object;
        ++at_;
        if (take('}')) return true;
        do {
            std::pair<std::string, JsonValue> member;
            space();
            if (!string(&member.first) || !take(':') || !value(&member.second, depth + 1)) return false;
            out->members.push_back(std::move(member));
        } while (take(','));
        return take('}');
    }

    bool array(JsonValue* out, int depth) {
        out->type = JsonValue::Type::Array;
        ++at_;
        if (take(']')) return true;
        do {
            out->items.emplace_back();
            if (!value(&out->items.back(), depth + 1)) return false;
        } while (take(','));
        return take(']');
    }

    bool number(JsonValue* out) {
        size_t start = at_;
        while (at_ < text_.size() && std::strchr("+-0123456789.eE", text_[at_])) ++at_;
        if (at_ == start) return false;
        out->type = JsonValue::Type::Number;
        out->text = text_.substr(start, at_ - start);
        out->number = jsonNumber(out->text);
        return true;
    }

    bool hex4(unsigned* out) {
        if (at_ + 4 > text_.size()) return false;
        *out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[at_++];
            *out <<= 4;
            if (c >= '0' && c <= '9') *out |= c - '0';
            else if (c >= 'a' && c <= 'f') *out |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') *out |= c - 'A' + 10;
            else return false;
        }
        return true;
    }

    static void utf8(unsigned cp, std::string* out) {
        if (cp < 0x80) {
            *out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            *out += static_cast<char>(0xC0 | (cp >> 6));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            *out += static_cast<char>(0xE0 | (cp >> 12));
            *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            *out += static_cast<char>(0xF0 | (cp >> 18));
            *out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string* out) {
        if (at_ >= text_.size() || text_[at_] != '"') return false;
        ++at_;
        while (at_ < text_.size()) {
            char c = text_[at_++];
            if (c == '"') return true;
            if (c != '\\') {
                *out += c;
                continue;
            }
            if (at_ >= text_.size()) return false;
            char e = text_[at_++];
            switch (e) {
                case '"': case '\\': case '/': *out += e; break;
                case 'b': *out += '\b'; break;
                case 'f': *out += '\f'; break;
                case 'n': *out += '\n'; break;
                case 'r': *out += '\r'; break;
                case 't': *out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // a surrogate pair
                        unsigned low;
                        if (!word("\\u") || !hex4(&low) || low < 0xDC00 || low > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    utf8(cp, out);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    const std::string& text_;
    size_t at_ = 0;
};

}  // namespace

bool parseJson(const std::string& text, JsonValue* out) {
    *out = JsonValue();
    return Parser(text).document(out);
}

}  // namespace gps
