// wire.cpp – see wire.h. fields() lists the JSON of one struct; Writer and Reader walk that same list.
#include "wire.h"

#include <algorithm>
#include <cstdint>

#include "json.h"

namespace gps {
namespace {

// ---- the lists. A visitor v takes v(key, member), v.optional(key, member) for a number where a negative value means
// "unknown" (null in JSON), and v.constant(key, value) for a value that is written but never stored.

template <class V> void fields(V& v, ErrorReply& r) { v("error", r.error); }

template <class V> void fields(V& v, ImportReply& r) {
    v("status", r.status); v("message", r.message); v("nativeName", r.nativeName);
    v("elements", r.elements); v("links", r.links); v("warnings", r.warnings);
}

template <class V> void fields(V& v, CapsFieldInfo& f) { v("name", f.name); v("type", f.type); v("kind", f.kind); v("values", f.values); }

template <class V> void fields(V& v, StructureInfo& s) { v("media", s.media); v("features", s.features); v("fields", s.fields); }

template <class V> void fields(V& v, FactoryEntry& e) {
    v("factory", e.factory); v("klass", e.klass); v("longName", e.longName); v("rank", e.rank); v("plugin", e.plugin);
    v("isSource", e.isSource); v("isSink", e.isSink);
    v("hasAlwaysSrc", e.hasAlwaysSrc); v("hasAlwaysSink", e.hasAlwaysSink);
    v("hasSometimesSrc", e.hasSometimesSrc); v("hasSometimesSink", e.hasSometimesSink);
    v("hasRequestSrc", e.hasRequestSrc); v("hasRequestSink", e.hasRequestSink); v("nvidia", e.nvidia);
}

template <class V> void fields(V& v, FactoryList& l) { v("elements", l.elements); }

template <class V> void fields(V& v, EnumValue& e) { v("nick", e.nick); v("name", e.name); v("value", e.value); }

template <class V> void fields(V& v, PropertyInfo& p) {
    v("name", p.name); v("nick", p.nick); v("blurb", p.blurb); v("kind", p.kind); v("typeName", p.typeName);
    v("default", p.defaultValue); v("min", p.min); v("max", p.max); v("enumValues", p.enumValues);
    v("readable", p.readable); v("writable", p.writable); v("construct", p.construct);
    v("constructOnly", p.constructOnly); v("deprecated", p.deprecated);
    v("mutableReady", p.mutableReady); v("mutablePaused", p.mutablePaused); v("mutablePlaying", p.mutablePlaying);
}

template <class V> void fields(V& v, PadTemplateInfo& t) {
    v("name", t.name); v("direction", t.direction); v("presence", t.presence); v("caps", t.caps);
    v("structures", t.structures); v("padProperties", t.padProperties);
}

template <class V> void fields(V& v, ElementInfo& e) {
    fields(v, e.entry);  // the factory's row, flat in the same object
    v("description", e.description); v("author", e.author);
    v("padTemplates", e.padTemplates); v("properties", e.properties);
}

template <class V> void fields(V& v, CompatibleFactories& c) { v("factories", c.factories); }

template <class V> void fields(V& v, CapsResult& c) { v("ok", c.ok); v("error", c.error); v("text", c.text); v("structures", c.structures); }

template <class V> void fields(V& v, VersionInfo::Gst& g) { v("version", g.version); v("major", g.major); v("minor", g.minor); v("micro", g.micro); }
template <class V> void fields(V& v, VersionInfo::Nvidia& n) { v("present", n.present); v("elements", n.elements); }
template <class V> void fields(V& v, VersionInfo::Plugins& p) { v("count", p.count); v("elements", p.elements); }
template <class V> void fields(V& v, VersionInfo& i) { v("gstreamer", i.gstreamer); v("nvidia", i.nvidia); v("plugins", i.plugins); }

template <class V> void fields(V& v, PadReport& p) {
    v("element", p.element); v("pad", p.pad); v("identity", p.identity); v("template", p.templateName);
    v("presence", p.presence); v("isSrc", p.isSrc); v("linked", p.linked); v("temporaryEnd", p.temporaryEnd);
    v("unknownData", p.unknownData); v("caps", p.caps); v("allowed", p.allowed); v("allowedStructures", p.allowedStructures);
}

template <class V> void fields(V& v, UpdateResult& r) {
    v("status", r.status); v("message", r.message); v("errorElement", r.errorElement); v("ms", r.ms);
    v("pads", r.pads); v("missing", r.missing); v("refused", r.refused);
    v.constant("complete", r.complete());  // for readers of the JSON; the struct computes it
}

template <class V> void fields(V& v, RunResult& r) { v("status", r.status); v("message", r.message); v("ms", r.ms); }

template <class V> void fields(V& v, ElementTelemetry& t) {
    v("element", t.element);
    v.optional("ageMs", t.ageMs); v.optional("queueMs", t.queueMs);
    v.optional("processingMs", t.processingMs); v.optional("pendingMs", t.pendingMs);
    v("processingCycles", t.processingCycles); v.optional("dropRatePercent", t.dropRatePercent);
    v("qosProcessed", t.qosProcessed); v("qosDropped", t.qosDropped);
    v("inputBuffers", t.inputBuffers); v("outputBuffers", t.outputBuffers);
}

template <class V> void fields(V& v, TelemetrySample& s) {
    v.constant("type", std::string("sample"));  // gps-monitor prints samples and, at the end, a RunResult
    v("elapsedMs", s.elapsedMs); v("elements", s.elements);
}

// ---- writing

class Writer {
public:
    JsonWriter w;

    template <class T> void object(const T& value) {
        w.beginObject();
        fields(*this, const_cast<T&>(value));  // the Writer only reads
        w.end();
    }
    void operator()(const char* key, const std::string& value) { w.field(key, value); }
    void operator()(const char* key, bool value) { w.field(key, value); }
    void operator()(const char* key, int value) { w.field(key, value); }
    void operator()(const char* key, double value) { w.field(key, value); }
    void operator()(const char* key, std::uint64_t value) { w.key(key).integer(static_cast<long long>(value)); }
    void operator()(const char* key, const std::vector<std::string>& values) {
        w.key(key).beginArray();
        for (const std::string& value : values) w.string(value);
        w.end();
    }
    template <class T> void operator()(const char* key, const std::vector<T>& values) {
        w.key(key).beginArray();
        for (const T& value : values) object(value);
        w.end();
    }
    template <class T> void operator()(const char* key, const T& value) {
        w.key(key);
        object(value);
    }
    void optional(const char* key, double value) {
        w.key(key);
        value < 0 ? w.null() : w.number(value);
    }
    template <class T> void constant(const char* key, const T& value) { (*this)(key, value); }
};

// ---- reading

// Numbers, booleans and strings all read as text: gps-inspect writes some numbers as text (SPEC 7).
std::string textOf(const JsonValue& v) {
    switch (v.type) {
        case JsonValue::Type::String: return v.text;
        case JsonValue::Type::Bool: return v.boolean ? "true" : "false";
        case JsonValue::Type::Number: return v.text;  // the number as it was written
        default: return "";
    }
}

double numberOf(const JsonValue& v) {
    if (v.type == JsonValue::Type::Number) return v.number;
    if (v.type == JsonValue::Type::String) return jsonNumber(v.text);
    return 0;
}

class Reader {
public:
    explicit Reader(const JsonValue& object) : object_(object) {}
    bool matched = true;  // false when a constant() field says this is another kind of message

    template <class T> static void read(const JsonValue& value, T& out) {
        if (value.type != JsonValue::Type::Object) return;
        Reader reader(value);
        fields(reader, out);
    }
    void operator()(const char* key, std::string& out) { if (const JsonValue* v = object_.find(key)) out = textOf(*v); }
    void operator()(const char* key, bool& out) {
        if (const JsonValue* v = object_.find(key); v && v->type == JsonValue::Type::Bool) out = v->boolean;
    }
    void operator()(const char* key, int& out) { if (const JsonValue* v = object_.find(key)) out = static_cast<int>(numberOf(*v)); }
    void operator()(const char* key, double& out) { if (const JsonValue* v = object_.find(key)) out = numberOf(*v); }
    void operator()(const char* key, std::uint64_t& out) {
        if (const JsonValue* v = object_.find(key)) out = static_cast<std::uint64_t>(std::max(0.0, numberOf(*v)));
    }
    void operator()(const char* key, std::vector<std::string>& out) {
        out.clear();
        if (const JsonValue* v = object_.find(key)) for (const JsonValue& item : v->items) out.push_back(textOf(item));
    }
    template <class T> void operator()(const char* key, std::vector<T>& out) {
        out.clear();
        if (const JsonValue* v = object_.find(key)) {
            for (const JsonValue& item : v->items) {
                out.emplace_back();
                read(item, out.back());
            }
        }
    }
    template <class T> void operator()(const char* key, T& out) { if (const JsonValue* v = object_.find(key)) read(*v, out); }
    void optional(const char* key, double& out) {
        const JsonValue* v = object_.find(key);
        out = v && v->type == JsonValue::Type::Number ? v->number : -1;
    }
    void constant(const char* key, const std::string& value) {
        const JsonValue* v = object_.find(key);
        matched = matched && v && textOf(*v) == value;
    }
    void constant(const char*, bool) {}  // computed by the struct itself

private:
    const JsonValue& object_;
};

}  // namespace

template <class T>
std::string toJson(const T& value) {
    Writer writer;
    writer.object(value);
    return writer.w.str();
}

template <class T>
bool fromJson(const std::string& text, T* out) {
    JsonValue root;
    if (!parseJson(text, &root) || root.type != JsonValue::Type::Object) return false;
    T value = *out;
    Reader reader(root);
    fields(reader, value);
    if (!reader.matched) return false;
    *out = std::move(value);
    return true;
}

#define GPS_WIRE(T) template std::string toJson<T>(const T&); template bool fromJson<T>(const std::string&, T*);
GPS_WIRE(ErrorReply)
GPS_WIRE(ImportReply)
GPS_WIRE(FactoryList)
GPS_WIRE(ElementInfo)
GPS_WIRE(CompatibleFactories)
GPS_WIRE(CapsResult)
GPS_WIRE(VersionInfo)
GPS_WIRE(UpdateResult)
GPS_WIRE(RunResult)
GPS_WIRE(TelemetrySample)
#undef GPS_WIRE

}  // namespace gps
