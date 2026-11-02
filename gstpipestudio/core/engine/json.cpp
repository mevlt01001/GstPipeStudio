// json.cpp – see json.h.
#include "json.h"

#include <cmath>
#include <cstdio>

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
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.15g", value);
    out_ += buf;
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

}  // namespace gps
