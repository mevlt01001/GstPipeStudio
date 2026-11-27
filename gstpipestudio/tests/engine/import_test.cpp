#include "importer.h"
#include "builder.h"
#include "testutil.h"

using namespace gpstest;

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    std::string error;
    gps::ImportResult imported;

    std::puts("-- native properties, linked pads and a renamed graph survive import");
    CHECK(gps::importLaunch("videotestsrc is-live=true ! queue ! fakesink sync=false", 300, &imported, &error));
    CHECK_EQ(imported.graph.elements.size(), 3u);
    CHECK_EQ(imported.graph.links.size(), 2u);
    CHECK(!imported.nativeName.empty());
    bool live = false, unsynced = false;
    for (const auto& e : imported.graph.elements) {
        if (e.factory == "videotestsrc") live = e.props.at("is-live") == "true";
        if (e.factory == "fakesink") unsynced = e.props.at("sync") == "false";
    }
    CHECK(live && unsynced);
    gps::Graph restored;
    CHECK(gps::Graph::fromConf(imported.graph.toConf(), &restored, &error));
    gps::UpdateOptions options; options.timeoutMs = 3000;
    auto updated = gps::update(restored, options, {});
    CHECK_EQ(updated.status, "ok"); CHECK(updated.complete());

    std::puts("-- tee request pads preserve both branches");
    CHECK(gps::importLaunch("videotestsrc is-live=true ! tee name=t t. ! queue ! fakesink sync=false t. ! queue ! fakesink sync=false", 300, &imported, &error));
    CHECK_EQ(imported.graph.links.size(), 5u);
    bool twoPads = false;
    for (const auto& e : imported.graph.elements) if (e.factory == "tee") {
        auto count = e.requestSrc.find("src_%u");
        twoPads = count != e.requestSrc.end() && count->second == 2;
    }
    CHECK(twoPads);
    updated = gps::update(imported.graph, options, {});
    CHECK_EQ(updated.status, "ok"); CHECK(updated.complete());

    std::puts("-- a short probe reveals decodebin's dynamic src pad");
    CHECK(gps::importLaunch("videotestsrc num-buffers=30 ! jpegenc ! decodebin ! fakesink sync=false", 700, &imported, &error));
    CHECK_EQ(imported.graph.links.size(), 3u);
    updated = gps::update(imported.graph, options, {});
    CHECK_EQ(updated.status, "ok"); CHECK(updated.complete());

    std::puts("-- parser errors do not replace the caller's graph");
    CHECK(!gps::importLaunch("not_a_real_element ! fakesink", 0, &imported, &error));
    CHECK(!error.empty());

    std::puts("-- multi-line gst-launch with env var, inline caps collapse and clean defaults");
    CHECK(gps::importLaunch(
        "GST_PLUGIN_PATH=/tmp gst-launch-1.0 -e \\\n"
        "  videotestsrc is-live=true ! video/x-raw,format=RGBA,width=640,height=480 ! videoconvert ! fakesink sync=false",
        500, &imported, &error));
    CHECK_EQ(imported.graph.elements.size(), 3u);
    CHECK_EQ(imported.graph.links.size(), 2u);
    CHECK_EQ(imported.graph.elements[0].factory, std::string("videotestsrc"));
    CHECK_EQ(imported.graph.elements[1].factory, std::string("videoconvert"));
    CHECK(gpstest::contains(imported.graph.links[0].caps, "format=(string)RGBA"));
    CHECK(!gpstest::contains(imported.graph.links[0].caps, "\\"));

    std::puts("-- exact-named Request sink pads (flvmux.audio) survive import and update");
    CHECK(gps::importLaunch("audiotestsrc num-buffers=5 ! audioconvert ! alawenc ! flvmux name=mux ! fakesink sync=false", 300, &imported, &error));
    bool hasExactAudioLink = false;
    for (const auto& l : imported.graph.links) {
        if (l.sinkPad == "audio") hasExactAudioLink = true;
    }
    CHECK(hasExactAudioLink);
    updated = gps::update(imported.graph, options, {});
    CHECK_EQ(updated.status, "ok");
    CHECK(updated.complete());
    return finish("import_test");
}
