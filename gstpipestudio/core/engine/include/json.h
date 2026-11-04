// json.h – the smallest JSON writer that does the job. No dependency, output is always valid UTF-8 JSON.
#pragma once

#include <string>
#include <vector>

namespace gps {

class JsonWriter {
public:
    JsonWriter& beginObject();
    JsonWriter& beginArray();
    JsonWriter& end();  // closes the innermost object or array
    JsonWriter& key(const std::string& name);  // only inside an object, followed by exactly one value
    JsonWriter& string(const std::string& value);
    JsonWriter& number(double value);  // NaN and infinity become null
    JsonWriter& integer(long long value);
    JsonWriter& boolean(bool value);
    JsonWriter& null();

    // key + value in one call
    JsonWriter& field(const std::string& name, const std::string& value) { return key(name).string(value); }
    JsonWriter& field(const std::string& name, const char* value) { return key(name).string(value); }
    JsonWriter& field(const std::string& name, bool value) { return key(name).boolean(value); }
    JsonWriter& field(const std::string& name, double value) { return key(name).number(value); }
    JsonWriter& field(const std::string& name, int value) { return key(name).integer(value); }

    const std::string& str() const { return out_; }

private:
    void beforeValue();

    std::string out_;
    std::vector<bool> first_;  // one entry per open object or array: nothing written inside yet
    std::vector<char> kind_;   // '{' or '['
    bool afterKey_ = false;
};

// The text as the inside of a JSON string literal (no surrounding quotes). Invalid UTF-8 becomes U+FFFD.
std::string jsonEscape(const std::string& text);

}  // namespace gps
