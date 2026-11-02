// inspector_test – what the registry says about elements: list, inspect, compatible, caps.
#include <algorithm>

#include "inspector.h"
#include "testutil.h"

using namespace gps;

static const FactoryEntry* findEntry(const std::vector<FactoryEntry>& all, const std::string& name) {
    for (const FactoryEntry& e : all) {
        if (e.factory == name) return &e;
    }
    return nullptr;
}

static const PropertyInfo* findProp(const std::vector<PropertyInfo>& props, const std::string& name) {
    for (const PropertyInfo& p : props) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

static const PadTemplateInfo* findTemplate(const ElementInfo& info, const std::string& name) {
    for (const PadTemplateInfo& t : info.padTemplates) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

static const CapsFieldInfo* findField(const StructureInfo& s, const std::string& name) {
    for (const CapsFieldInfo& f : s.fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    // ---- list
    std::vector<FactoryEntry> all = listFactories();
    CHECK(all.size() > 200);
    CHECK(std::is_sorted(all.begin(), all.end(), [](const FactoryEntry& a, const FactoryEntry& b) { return a.factory < b.factory; }));
    const FactoryEntry* vts = findEntry(all, "videotestsrc");
    CHECK(vts != nullptr);
    if (vts) {
        CHECK(vts->isSource);
        CHECK(!vts->isSink);
        CHECK(!vts->nvidia);
        CHECK_EQ(vts->plugin, "videotestsrc");
        CHECK(vts->klass.find("Source") != std::string::npos);
    }
    const FactoryEntry* fake = findEntry(all, "fakesink");
    CHECK(fake && fake->isSink && !fake->isSource);
    const FactoryEntry* tee = findEntry(all, "tee");
    CHECK(tee && tee->hasRequestSrc && !tee->isSource && !tee->isSink);
    const FactoryEntry* conv = findEntry(all, "videoconvert");
    CHECK(conv && !conv->hasRequestSrc);
    const FactoryEntry* nv = findEntry(all, "nvvideoconvert");
    CHECK(nv && nv->nvidia);
    int nvidia = 0;
    for (const FactoryEntry& e : all) nvidia += e.nvidia;
    CHECK(nvidia >= 20);  // DeepStream 7.1 image: dozens of nv* elements
    std::printf("  info: %zu factories, %d nvidia\n", all.size(), nvidia);

    // ---- inspect: properties
    ElementInfo info;
    std::string error;
    CHECK(inspect("videotestsrc", &info, &error));
    const PropertyInfo* pattern = findProp(info.properties, "pattern");
    CHECK(pattern != nullptr);
    if (pattern) {
        CHECK_EQ(pattern->kind, "enum");
        CHECK_EQ(pattern->defaultValue, "smpte");
        bool ball = false;
        for (const EnumValue& v : pattern->enumValues) ball = ball || (v.nick == "ball" && v.value == "18");
        CHECK(ball);
        CHECK(pattern->writable && pattern->readable);
        CHECK(!pattern->mutablePlaying);  // the flags are reported as GStreamer has them
    }
    const PropertyInfo* frames = findProp(info.properties, "num-buffers");
    CHECK(frames && frames->kind == "int" && frames->min == "-1" && frames->defaultValue == "-1");
    CHECK(!findProp(info.properties, "name"));    // GstObject's own property is not the Plan's business
    CHECK(!findProp(info.properties, "parent"));
    const PropertyInfo* live = findProp(info.properties, "is-live");
    CHECK(live && live->kind == "boolean" && live->defaultValue == "false");
    CHECK(info.entry.isSource);
    CHECK(info.description.size() > 3);

    CHECK(inspect("audiotestsrc", &info, &error));
    const PropertyInfo* volume = findProp(info.properties, "volume");
    CHECK(volume && volume->kind == "double" && volume->min == "0" && volume->max == "1" && volume->defaultValue == "0.8");

    CHECK(inspect("queue", &info, &error));
    const PropertyInfo* buffers = findProp(info.properties, "max-size-buffers");
    CHECK(buffers && buffers->kind == "uint" && buffers->defaultValue == "200" && buffers->min == "0");
    const PropertyInfo* leaky = findProp(info.properties, "leaky");
    CHECK(leaky && leaky->kind == "enum" && leaky->enumValues.size() == 3);
    CHECK(buffers && buffers->mutablePlaying);  // flags are copied as GStreamer has them
    CHECK(volume && !volume->mutablePlaying);

    CHECK(inspect("compare", &info, &error));
    CHECK(findTemplate(info, "sink") && findTemplate(info, "check"));  // two Always sinks

    // ---- inspect: pad templates and caps structures
    CHECK(inspect("tee", &info, &error));
    const PadTemplateInfo* teeSrc = findTemplate(info, "src_%u");
    CHECK(teeSrc && teeSrc->direction == "src" && teeSrc->presence == "request");
    const PadTemplateInfo* teeSink = findTemplate(info, "sink");
    CHECK(teeSink && teeSink->direction == "sink" && teeSink->presence == "always" && teeSink->caps == "ANY");
    CHECK(teeSink && teeSink->structures.empty());  // ANY has no structures

    CHECK(inspect("compositor", &info, &error));
    const PadTemplateInfo* mixSink = findTemplate(info, "sink_%u");
    CHECK(mixSink && mixSink->presence == "request" && mixSink->direction == "sink");
    if (mixSink) {
        const PropertyInfo* xpos = findProp(mixSink->padProperties, "xpos");
        CHECK(xpos && xpos->kind == "int" && xpos->writable);
        CHECK(findProp(mixSink->padProperties, "alpha") != nullptr);
        // properties of GObject, GstObject and GstPad themselves are left out
        for (const char* base : {"name", "parent", "template", "direction", "offset"}) CHECK(!findProp(mixSink->padProperties, base));
    }
    const PadTemplateInfo* mixSrc = findTemplate(info, "src");
    CHECK(mixSrc && mixSrc->presence == "always");
    if (mixSrc) CHECK(mixSrc->padProperties.empty() || !findProp(mixSrc->padProperties, "xpos"));

    CHECK(inspect("videoconvert", &info, &error));
    const PadTemplateInfo* convSrc = findTemplate(info, "src");
    CHECK(convSrc && !convSrc->structures.empty());
    if (convSrc && !convSrc->structures.empty()) {
        const StructureInfo& s = convSrc->structures[0];
        CHECK_EQ(s.media, "video/x-raw");
        const CapsFieldInfo* format = findField(s, "format");
        CHECK(format && format->kind == "list" && format->type == "gchararray" && format->values.size() > 10);
        const CapsFieldInfo* width = findField(s, "width");
        CHECK(width && width->kind == "range" && width->type == "gint" && width->values.size() == 2);
        if (width) CHECK_EQ(width->values[0], "1");
        const CapsFieldInfo* rate = findField(s, "framerate");
        CHECK(rate && rate->kind == "range" && rate->type == "GstFraction");
        if (rate) CHECK_EQ(rate->values[0], "0/1");
    }

    CHECK(inspect("nvvideoconvert", &info, &error));
    bool nvmem = false;
    for (const PadTemplateInfo& t : info.padTemplates) {
        for (const StructureInfo& s : t.structures) {
            for (const std::string& f : s.features) nvmem = nvmem || f == "memory:NVMM";
        }
    }
    CHECK(nvmem);

    CHECK(!inspect("no-such-element-xyz", &info, &error));
    CHECK(gpstest::contains(error, "no-such-element-xyz"));

    // ---- compatible
    std::vector<std::string> fit;
    CHECK(compatibleFactories("video/x-raw, format=(string)I420, width=(int)320, height=(int)240", &fit, &error));
    auto has = [&](const std::string& n) { return std::find(fit.begin(), fit.end(), n) != fit.end(); };
    CHECK(has("videoconvert"));
    CHECK(has("fakesink"));
    CHECK(has("queue"));
    CHECK(!has("audioconvert"));   // wrong media type
    CHECK(!has("videotestsrc"));   // no sink template
    // rank first (high to low), then name
    bool ordered = true;
    for (size_t i = 1; i < fit.size(); ++i) {
        const FactoryEntry* a = findEntry(all, fit[i - 1]);
        const FactoryEntry* b = findEntry(all, fit[i]);
        ordered = ordered && (a->rank > b->rank || (a->rank == b->rank && a->factory < b->factory));
    }
    CHECK(ordered);
    CHECK(fit.size() > 10 && fit.size() < all.size());

    CHECK(compatibleFactories("audio/x-raw", &fit, &error));
    CHECK(has("audioconvert") && !has("videoconvert"));

    CHECK(compatibleFactories("", &fit, &error));
    CHECK(has("fakesink") && has("videoconvert") && has("audioconvert") && !has("videotestsrc"));
    size_t withSink = fit.size();
    CHECK(withSink > 100);
    CHECK(!compatibleFactories("video/x-raw, width=(int", &fit, &error));

    // ---- parseCaps
    std::string text;
    std::vector<StructureInfo> structures;
    CHECK(parseCaps("video/x-raw,width=(int)640", &text, &structures, &error));
    CHECK_EQ(text, "video/x-raw, width=(int)640");
    CHECK(structures.size() == 1 && structures[0].media == "video/x-raw" && structures[0].fields.size() == 1);
    if (!structures.empty() && !structures[0].fields.empty()) {
        CHECK_EQ(structures[0].fields[0].type, "gint");
        CHECK_EQ(structures[0].fields[0].kind, "fixed");
        CHECK_EQ(structures[0].fields[0].values[0], "640");
    }
    CHECK(parseCaps("video/x-raw(memory:NVMM), format=(string){ NV12, RGBA }, width=(int)[ 16, 4096, 2 ], note=(string)\"a b\"", &text,
                    &structures, &error));
    if (!structures.empty()) {
        CHECK(structures[0].features == std::vector<std::string>{"memory:NVMM"});
        const CapsFieldInfo* f = findField(structures[0], "format");
        CHECK(f && f->kind == "list" && f->values == std::vector<std::string>({"NV12", "RGBA"}));
        const CapsFieldInfo* w = findField(structures[0], "width");
        CHECK(w && w->kind == "range" && w->values == std::vector<std::string>({"16", "4096", "2"}));  // min, max, step when not 1
        const CapsFieldInfo* n = findField(structures[0], "note");
        CHECK(n && n->kind == "fixed" && n->values[0] == "a b");  // strings come raw, the GUI does the quoting (capsString)
    }
    CHECK(parseCaps("video/x-raw, framerate=(fraction)[ 0/1, 60/1 ]; audio/x-raw", &text, &structures, &error));
    CHECK(structures.size() == 2 && structures[1].media == "audio/x-raw");
    CHECK(parseCaps("ANY", &text, &structures, &error) && structures.empty());
    CHECK(!parseCaps("video/x-raw, width=(int", &text, &structures, &error));
    CHECK(!error.empty());
    CHECK(!parseCaps("", &text, &structures, &error));

    // ---- JSON text (the parse check is in the cli tests)
    CHECK(gpstest::contains(listJson(all), "\"factory\":\"videotestsrc\""));
    CHECK(gpstest::contains(capsJson(false, "bad \"caps\"", {}), "\"ok\":false,\"error\":\"bad \\\"caps\\\"\""));
    return finish("inspector_test");
}
