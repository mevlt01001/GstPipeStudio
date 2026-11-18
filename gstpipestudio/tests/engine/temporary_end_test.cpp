// Remove a real registry feature, then verify failed temporary-end construction releases its objects.
#include "builder.h"
#include "check.h"
#include <cstring>

namespace {
int created = 0, destroyed = 0;
bool observing = false;

void finalized(gpointer, GObject*) { ++destroyed; }
void observe(GstElement* element) {
    if (!element) return;
    ++created;
    // A weak notification observes destruction without keeping the object alive.
    g_object_weak_ref(G_OBJECT(element), finalized, nullptr);
}
}

extern "C" GstElement* __real_gst_element_factory_make(const gchar*, const gchar*);
extern "C" GstElement* __real_gst_bin_new(const gchar*);

extern "C" GstElement* __wrap_gst_element_factory_make(const gchar* factory, const gchar* name) {
    GstElement* element = __real_gst_element_factory_make(factory, name);
    if (observing && !name &&
        (std::strcmp(factory, "queue") == 0 || std::strcmp(factory, "typefind") == 0 || std::strcmp(factory, "fakesink") == 0))
        observe(element);
    return element;
}

extern "C" GstElement* __wrap_gst_bin_new(const gchar* name) {
    GstElement* element = __real_gst_bin_new(name);
    if (observing && name && g_str_has_prefix(name, "temp_")) observe(element);
    return element;
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    if (argc != 2) return 2;

    // Load all factories before removing one, so lazy plugin loading cannot restore it.
    for (const char* factory : {"queue", "typefind", "fakesink"}) {
        GstElement* element = gst_element_factory_make(factory, nullptr);
        CHECK(element != nullptr);
        if (element) gst_object_unref(element);
    }
    GstElementFactory* missing = gst_element_factory_find(argv[1]);
    CHECK(missing != nullptr);
    if (!missing) return finish("temporary_end_test");
    gst_registry_remove_feature(gst_registry_get(), GST_PLUGIN_FEATURE(missing));
    GstElementFactory* absent = gst_element_factory_find(argv[1]);
    CHECK(absent == nullptr);
    if (absent) gst_object_unref(absent);

    gps::Graph plan;
    gps::Element source;
    source.name = "src";
    source.factory = "identity";
    plan.elements.push_back(source);
    gps::BuildOptions options;
    options.temporaryEnds = true;

    for (int attempt = 1; attempt <= 3; ++attempt) {
        GstElement* pipeline = gst_pipeline_new("missing-plugin-test");
        observing = true;
        {
            gps::Builder builder(plan, GST_BIN(pipeline), options, {});
            CHECK(!builder.build());
            CHECK_EQ(builder.failure().element, "src");
            CHECK_EQ(builder.failure().message,
                     "unable to build a temporary end: queue, typefind or fakesink is missing");
        }
        observing = false;
        gst_object_unref(pipeline);
        CHECK_EQ(created, attempt * 3);  // temporary bin plus the two available elements
        CHECK_EQ(destroyed, created);
    }

    gst_object_unref(missing);
    gst_deinit();
    return finish("temporary_end_test");
}
