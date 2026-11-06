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
    return 0;
}
