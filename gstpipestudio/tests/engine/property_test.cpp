// property_test – invalid property values fail the update and name the right element and property (SPEC 5).
#include "testutil.h"

using namespace gpstest;
using gps::UpdateResult;

// One element "e" with one property value; an element that has a sink pad gets a source in front of it.
static UpdateResult with(const std::string& factory, const std::string& property, const std::string& value) {
    P p;
    p.el("e", factory, {{property, value}});
    GstElementFactory* f = gst_element_factory_find(factory.c_str());
    bool needsInput = false;
    for (const GList* t = gst_element_factory_get_static_pad_templates(f); t; t = t->next) {
        needsInput = needsInput || static_cast<const GstStaticPadTemplate*>(t->data)->direction == GST_PAD_SINK;
    }
    gst_object_unref(f);
    if (needsInput) p.el("feed", "videotestsrc", {{"num-buffers", "3"}}).link("feed", "src", "e", "sink");
    return update(p.plan, 5000);
}

// The update must fail, name element "e" and say "e.<property>: '<value>' gecersiz (<reason part>)".
static void rejected(const std::string& label, const std::string& factory, const std::string& property,
                     const std::string& value, const std::string& reasonPart) {
    std::printf("-- rejected: %s\n", label.c_str());
    UpdateResult r = with(factory, property, value);
    CHECK_EQ(r.status, "error");
    CHECK_EQ(r.errorElement, "e");
    std::string prefix = "e." + property + ": '" + value + "' gecersiz (";
    CHECK(startsWith(r.message, prefix));
    CHECK(contains(r.message, reasonPart));
    if (!startsWith(r.message, prefix) || !contains(r.message, reasonPart)) std::printf("    message was: %s\n", r.message.c_str());
    CHECK(r.pads.empty());
}

static void accepted(const std::string& label, const std::string& factory, const std::string& property, const std::string& value) {
    std::printf("-- accepted: %s\n", label.c_str());
    UpdateResult r = with(factory, property, value);
    CHECK_EQ(brief(r), "ok");
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    // GStreamer alone ignores these silently (volume=5) or turns them into something else (-5 -> 4294967291, "" -> 0)
    rejected("float out of range", "audiotestsrc", "volume", "5", "aralik disinda");
    rejected("float out of range, with the allowed range", "audiotestsrc", "volume", "-1", "0 .. 1");
    rejected("negative on an unsigned property", "queue", "max-size-buffers", "-5", "negatif");
    rejected("negative on a 64 bit unsigned property", "queue", "max-size-time", "-1", "negatif");
    rejected("empty text on an int", "videotestsrc", "num-buffers", "", "bos");
    rejected("blank text on an int", "videotestsrc", "num-buffers", "   ", "bos");
    rejected("empty text on a float", "audiotestsrc", "volume", "", "bos");
    rejected("empty text on a boolean", "videotestsrc", "is-live", "", "bos");
    rejected("empty text on an enum", "videotestsrc", "pattern", "", "bos");
    rejected("unknown enum name", "videotestsrc", "pattern", "nonsense", "okunamadi");
    rejected("text on a number", "videotestsrc", "num-buffers", "abc", "okunamadi");
    rejected("int out of range for gint", "videotestsrc", "num-buffers", "99999999999", "");
    rejected("bad boolean", "videotestsrc", "is-live", "maybe", "okunamadi");
    rejected("property that does not exist", "videotestsrc", "no-such-property", "1", "yok");
    rejected("the GstObject name property", "videotestsrc", "name", "other", "GstObject");
    rejected("construct-only or read-only is not writable", "queue", "current-level-buffers", "3", "yazilamaz");

    accepted("float in range", "audiotestsrc", "volume", "0.5");
    accepted("float at the edge", "audiotestsrc", "volume", "1");
    accepted("unsigned zero", "queue", "max-size-buffers", "0");
    accepted("enum by nick", "videotestsrc", "pattern", "ball");
    accepted("enum by number", "videotestsrc", "pattern", "18");
    accepted("enum by name", "videotestsrc", "pattern", "Moving ball");
    accepted("boolean", "videotestsrc", "is-live", "false");
    {
        std::puts("-- accepted: an empty text on a string property (filesrc then fails at run time, not at the value)");
        UpdateResult r = with("filesrc", "location", "");
        CHECK(!contains(r.message, "gecersiz"));
        CHECK_EQ(r.errorElement, "e");
    }
    accepted("negative on a signed property", "videotestsrc", "num-buffers", "-1");

    std::puts("-- two elements: the broken one is named, not the first");
    {
        P p;
        p.el("good", "videotestsrc", {{"num-buffers", "3"}}).el("bad", "audiotestsrc", {{"volume", "99"}});
        UpdateResult r = update(p.plan);
        CHECK_EQ(r.status, "error");
        CHECK_EQ(r.errorElement, "bad");
        CHECK(startsWith(r.message, "bad.volume: '99' gecersiz ("));
    }

    std::puts("-- a bad value writes nothing: the element keeps its default");
    {
        GstElement* e = gst_element_factory_make("audiotestsrc", nullptr);
        std::string reason;
        CHECK(!gps::setProperty(G_OBJECT(e), "volume", "5", &reason));
        gdouble volume = 0;
        g_object_get(e, "volume", &volume, nullptr);
        CHECK(volume > 0.79 && volume < 0.81);
        CHECK(gps::setProperty(G_OBJECT(e), "volume", "0.25", &reason));
        g_object_get(e, "volume", &volume, nullptr);
        CHECK(volume > 0.24 && volume < 0.26);
        gst_object_unref(e);
    }

    std::puts("-- sink pad properties go through the same check");
    {
        P p;
        p.el("a", "videotestsrc", {{"num-buffers", "3"}}).el("mix", "compositor").el("out", "fakesink");
        p.link("a", "src", "mix", "sink_%u", "", {{"xpos", "abc"}}).link("mix", "src", "out", "sink");
        UpdateResult r = update(p.plan, 5000);
        CHECK_EQ(r.status, "error");
        CHECK_EQ(r.errorElement, "mix");
        CHECK(startsWith(r.message, "mix.sink_%u::xpos: 'abc' gecersiz ("));

        P q;
        q.el("a", "videotestsrc", {{"num-buffers", "3"}}).el("mix", "compositor").el("out", "fakesink");
        q.link("a", "src", "mix", "sink_%u", "", {{"alpha", "3"}}).link("mix", "src", "out", "sink");
        r = update(q.plan, 5000);
        CHECK_EQ(r.status, "error");
        CHECK(startsWith(r.message, "mix.sink_%u::alpha: '3' gecersiz ("));

        P s;
        s.el("a", "videotestsrc", {{"num-buffers", "3"}}).el("mix", "compositor").el("out", "fakesink");
        s.link("a", "src", "mix", "sink_%u", "", {{"nothing", "1"}}).link("mix", "src", "out", "sink");
        r = update(s.plan, 5000);
        CHECK_EQ(r.status, "error");
        CHECK(contains(r.message, "yok"));
    }
    return finish("property_test");
}
