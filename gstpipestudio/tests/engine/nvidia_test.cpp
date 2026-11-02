// nvidia_test – the engine on DeepStream elements: NVMM caps features and a Request template that wants a number.
// Skipped (and says so) when the elements or the sample stream are not there.
#include <sys/stat.h>

#include "testutil.h"

using namespace gpstest;
using gps::UpdateResult;

static bool factoryExists(const char* name) {
    GstElementFactory* f = gst_element_factory_find(name);
    if (f) gst_object_unref(f);
    return f != nullptr;
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    if (!factoryExists("nvstreammux") || !factoryExists("nvvideoconvert")) {
        std::puts("SKIP: no DeepStream elements");
        return 0;
    }

    std::puts("-- nvstreammux: its Request pads want a number (sink_0), the bare template is refused");
    {
        const char* nvmm = "video/x-raw(memory:NVMM), format=(string)NV12";
        P p;
        p.el("a", "videotestsrc", {{"num-buffers", "5"}}).el("b", "videotestsrc", {{"num-buffers", "5"}});
        p.el("ca", "nvvideoconvert").el("cb", "nvvideoconvert");
        p.el("mux", "nvstreammux", {{"batch-size", "2"}, {"width", "320"}, {"height", "240"}, {"batched-push-timeout", "100000"}});
        p.el("out", "fakesink");
        p.link("a", "src", "ca", "sink").link("b", "src", "cb", "sink");
        p.link("ca", "src", "mux", "sink_%u", nvmm).link("cb", "src", "mux", "sink_%u", nvmm).link("mux", "src", "out", "sink");
        Log log;
        UpdateResult r = update(p.plan, 20000, &log);
        CHECK_EQ(brief(r), "ok");
        CHECK(r.refused.empty());
        CHECK(r.complete());
        CHECK(contains(log.text, "'sink_%u' kabul edilmedi, 'sink_0' istendi"));
        CHECK(contains(log.text, "'sink_1' istendi"));
        int linked = 0;
        for (const gps::PadReport& pr : r.pads) linked += (pr.element == "mux" && !pr.isSrc && pr.linked);
        CHECK_EQ(linked, 2);
        const gps::PadReport* muxOut = pad(r, "mux", "src");
        CHECK(muxOut && contains(muxOut->caps, "memory:NVMM") && contains(muxOut->caps, "width=(int)320"));
    }

    std::puts("-- NVMM features in the caps structures of a free pad");
    {
        P p;
        p.el("a", "videotestsrc", {{"num-buffers", "5"}}).el("conv", "nvvideoconvert").link("a", "src", "conv", "sink");
        UpdateResult r = update(p.plan, 20000);
        CHECK_EQ(brief(r), "ok");
        const gps::PadReport* end = pad(r, "conv", "src");
        CHECK(end && end->temporaryEnd);
        bool nvmm = false;
        if (end) {
            for (const gps::StructureInfo& s : end->allowedStructures) {
                for (const std::string& f : s.features) nvmm = nvmm || f == "memory:NVMM";
            }
        }
        CHECK(nvmm);  // the GUI can offer the NVMM variants
    }

    const char* sample = "/opt/nvidia/deepstream/deepstream/samples/streams/sample_720p.mp4";
    struct stat st;
    if (stat(sample, &st) != 0 || !factoryExists("nvv4l2decoder")) {
        std::puts("SKIP: no sample stream or decoder for the decode path");
        return finish("nvidia_test");
    }
    std::puts("-- mp4 -> qtdemux -> h264parse -> nvv4l2decoder -> nvstreammux -> nvvideoconvert (GPU decode)");
    {
        P p;
        p.el("file", "filesrc", {{"location", sample}}).el("demux", "qtdemux").el("parse", "h264parse").el("dec", "nvv4l2decoder");
        p.el("mux", "nvstreammux", {{"batch-size", "1"}, {"width", "640"}, {"height", "360"}}).el("conv", "nvvideoconvert");
        p.link("file", "src", "demux", "sink").link("demux", "video_%u#0", "parse", "sink").link("parse", "src", "dec", "sink");
        p.link("dec", "src", "mux", "sink_%u").link("mux", "src", "conv", "sink");
        UpdateResult r = update(p.plan, 30000);
        CHECK_EQ(brief(r), "ok");
        CHECK(r.refused.empty() && r.missing.empty());
        const gps::PadReport* dec = pad(r, "dec", "src");
        CHECK(dec && dec->linked && contains(dec->caps, "memory:NVMM") && contains(dec->caps, "width=(int)1280"));
        const gps::PadReport* conv = pad(r, "conv", "src");
        CHECK(conv && conv->temporaryEnd && contains(conv->caps, "width=(int)640"));
        const gps::PadReport* audio = pad(r, "demux", "audio_%u#0");
        CHECK(audio && audio->temporaryEnd && contains(audio->caps, "audio/mpeg"));
        CHECK(!r.complete());  // two free pads
    }
    return finish("nvidia_test");
}
