// update_test – every scenario of SPEC 5 against real GStreamer.
#include "testutil.h"

using namespace gpstest;
using gps::PadReport;
using gps::UpdateResult;

static std::string av() { return tempDir() + "/av.avi"; }

static void chain() {
    std::puts("-- chain");
    P p;
    p.el("src", "videotestsrc", {{"num-buffers", "5"}}).el("conv", "videoconvert").link("src", "src", "conv", "sink");
    UpdateResult r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    const PadReport* srcPad = pad(r, "src", "src");
    const PadReport* end = pad(r, "conv", "src");
    CHECK(srcPad && srcPad->linked && !srcPad->temporaryEnd);
    CHECK(srcPad && startsWith(srcPad->caps, "video/x-raw"));
    CHECK(end && !end->linked && end->temporaryEnd && !end->unknownData);
    CHECK(end && startsWith(end->caps, "video/x-raw"));
    CHECK(end && end->presence == "always" && end->templateName == "src");
    CHECK(end && !end->allowedStructures.empty());
    CHECK(!r.complete());
    const PadReport* sinkPad = pad(r, "conv", "sink", false);
    CHECK(sinkPad && sinkPad->linked && sinkPad->presence == "always");

    p.el("out", "fakesink").link("conv", "src", "out", "sink");  // closing the chain
    r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    CHECK(r.complete());
    for (const PadReport& pr : r.pads) CHECK(!pr.temporaryEnd);
}

static void demux() {
    std::puts("-- demux with Sometimes pads");
    P p;
    p.el("file", "filesrc", {{"location", av()}}).el("dm", "avidemux").link("file", "src", "dm", "sink");
    UpdateResult r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    const PadReport* v = pad(r, "dm", "video_%u#0");
    const PadReport* a = pad(r, "dm", "audio_%u#0");
    CHECK(v && v->temporaryEnd && v->presence == "sometimes" && v->templateName == "video_%u" && !v->linked);
    CHECK(a && a->temporaryEnd && a->presence == "sometimes" && a->templateName == "audio_%u");
    CHECK(v && startsWith(v->caps, "image/jpeg"));
    CHECK(a && startsWith(a->caps, "audio/x-raw"));
    CHECK(!r.complete());
    CHECK(r.missing.empty() && r.refused.empty());

    std::puts("-- linking a Sometimes pad");
    p.el("jd", "jpegdec").link("dm", "video_%u#0", "jd", "sink");
    r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    v = pad(r, "dm", "video_%u#0");
    CHECK(v && v->linked && !v->temporaryEnd);
    const PadReport* raw = pad(r, "jd", "src");
    CHECK(raw && raw->temporaryEnd && startsWith(raw->caps, "video/x-raw"));
    a = pad(r, "dm", "audio_%u#0");
    CHECK(a && a->temporaryEnd);  // the other one stays free

    std::puts("-- both linked, everything closed");
    p.el("qa", "queue").el("ka", "fakesink").el("kv", "fakesink");
    p.link("dm", "audio_%u#0", "qa", "sink").link("qa", "src", "ka", "sink").link("jd", "src", "kv", "sink");
    r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    CHECK(r.complete());
}

static void teeRequestSrc() {
    std::puts("-- tee with requestSrc=2");
    P p;
    p.el("src", "videotestsrc", {{"num-buffers", "5"}}).el("t", "tee").req("t", "src_%u", 2).link("src", "src", "t", "sink");
    UpdateResult r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    const PadReport* a = pad(r, "t", "src_%u#0");
    const PadReport* b = pad(r, "t", "src_%u#1");
    CHECK(a && a->temporaryEnd && a->presence == "request" && startsWith(a->caps, "video/x-raw"));
    CHECK(b && b->temporaryEnd && b->presence == "request" && startsWith(b->caps, "video/x-raw"));
    CHECK(a && b && a->pad != b->pad);
    CHECK(!pad(r, "t", "src_%u#2"));

    std::puts("-- tee, one branch linked: the link goes to the first pad");
    p.el("q", "queue").el("k", "fakesink").link("t", "src_%u#1", "q", "sink").link("q", "src", "k", "sink");
    r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    CHECK(pad(r, "t", "src_%u#1") && pad(r, "t", "src_%u#1")->linked);
    CHECK(pad(r, "t", "src_%u#0") && pad(r, "t", "src_%u#0")->temporaryEnd);

    std::puts("-- requestSrc=0: a tee without src pad. GStreamer answers not-linked, nothing special is done");
    P none;
    none.el("src", "videotestsrc", {{"num-buffers", "5"}}).el("t", "tee").link("src", "src", "t", "sink");
    r = update(none.plan);
    CHECK_EQ(r.status, "error");
    CHECK_EQ(r.errorElement, "src");
    CHECK(gpstest::contains(r.message, "not-linked"));
    CHECK(!pad(r, "t", "src_%u#0"));
}

static void compositor() {
    std::puts("-- compositor: two Request sinks, a caps choice and a sink pad property");
    P p;
    p.el("a", "videotestsrc", {{"num-buffers", "5"}}).el("b", "videotestsrc", {{"num-buffers", "5"}});
    p.el("mix", "compositor").el("out", "fakesink");
    p.link("a", "src", "mix", "sink_%u");
    p.link("b", "src", "mix", "sink_%u", "video/x-raw, width=(int)160, height=(int)120", {{"xpos", "50"}});
    p.link("mix", "src", "out", "sink");
    Log log;
    UpdateResult r = update(p.plan, 10000, &log);
    CHECK_EQ(brief(r), "ok");
    CHECK(r.complete());
    CHECK(r.refused.empty());
    const PadReport* b = pad(r, "b", "src");
    CHECK(b && b->linked && gpstest::contains(b->caps, "width=(int)160"));
    int linkedSinks = 0;
    for (const PadReport& pr : r.pads) {
        if (pr.element == "mix" && !pr.isSrc && pr.linked) {
            ++linkedSinks;
            CHECK_EQ(pr.identity, "sink_%u");  // Request sink pads are named by template (SPEC 3)
            CHECK_EQ(pr.presence, "request");
        }
    }
    CHECK_EQ(linkedSinks, 2);
    CHECK(gpstest::contains(log.text, "::xpos = 50"));  // the pad property was written
}

static void freeSecondSink() {
    std::puts("-- an element with a free second Always sink");
    P p;
    p.el("v", "videotestsrc", {{"num-buffers", "5"}}).el("ov", "cc708overlay").el("out", "fakesink");
    p.link("v", "src", "ov", "video_sink").link("ov", "src", "out", "sink");
    UpdateResult r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    const PadReport* cc = pad(r, "ov", "cc_sink", false);
    CHECK(cc && !cc->linked && cc->presence == "always");
    const PadReport* video = pad(r, "ov", "video_sink", false);
    CHECK(video && video->linked);
    CHECK(r.complete());  // a free sink pad does not make the pipeline incomplete

    // GStreamer's answer for an element that needs both inputs: it waits. We report that and add nothing.
    std::puts("-- the same with an element that waits for its second input: timeout, pads still reported");
    P q;
    q.el("v", "videotestsrc", {{"num-buffers", "5"}}).el("cmp", "compare").el("out", "fakesink");
    q.link("v", "src", "cmp", "sink").link("cmp", "src", "out", "sink");
    r = update(q.plan, 1200);
    CHECK_EQ(r.status, "timeout");
    CHECK(pad(r, "cmp", "check", false) && !pad(r, "cmp", "check", false)->linked);
}

static void unknownData() {
    std::puts("-- unknown data");
    P p;
    p.el("f", "filesrc", {{"location", tempDir() + "/random.bin"}});
    UpdateResult r = update(p.plan);
    CHECK_EQ(brief(r), "ok");  // typefind failing is not a failed update (SPEC 5.5)
    const PadReport* f = pad(r, "f", "src");
    CHECK(f && f->unknownData && f->temporaryEnd && f->caps.empty());
    CHECK(!r.complete());

    std::puts("-- unknown data next to a healthy branch");
    p.el("v", "videotestsrc", {{"num-buffers", "5"}}).el("k", "fakesink").link("v", "src", "k", "sink");
    r = update(p.plan, 5000);
    CHECK_EQ(brief(r), "ok");
    CHECK(pad(r, "f", "src") && pad(r, "f", "src")->unknownData);
    CHECK(pad(r, "v", "src") && !pad(r, "v", "src")->unknownData && pad(r, "v", "src")->linked);
    CHECK(!r.complete());
    CHECK(r.ms < 3000);  // the dead end must not hold the other branch until the timeout

    std::puts("-- a known file is not unknown");
    P q;
    q.el("f", "filesrc", {{"location", av()}});
    r = update(q.plan);
    CHECK_EQ(brief(r), "ok");
    CHECK(pad(r, "f", "src") && !pad(r, "f", "src")->unknownData);
    CHECK(pad(r, "f", "src") && pad(r, "f", "src")->caps == "video/x-msvideo");  // read at the end's fakesink (SPEC 5.6)
}

static void wrongLocation() {
    std::puts("-- wrong file location");
    P p;
    p.el("src", "filesrc", {{"location", tempDir() + "/does-not-exist.avi"}}).el("dm", "avidemux").link("src", "src", "dm", "sink");
    UpdateResult r = update(p.plan);
    CHECK_EQ(r.status, "error");
    CHECK_EQ(r.errorElement, "src");
    CHECK(gpstest::contains(r.message, "No such file"));
    CHECK(!r.complete());
}

static void padMissing() {
    std::puts("-- pad-missing: the plan links an audio pad the file does not have");
    P p;
    p.el("file", "filesrc", {{"location", tempDir() + "/video_only.avi"}}).el("dm", "avidemux");
    p.el("q", "queue").el("k", "fakesink");
    p.link("file", "src", "dm", "sink").link("dm", "audio_%u#0", "q", "sink").link("q", "src", "k", "sink");
    UpdateResult r = update(p.plan, 8000);
    CHECK_EQ(r.status, "pad-missing");
    CHECK(r.missing == std::vector<std::string>{"dm.audio_%u#0"});
    CHECK(r.ms < 3000);  // answered at no-more-pads, not by the timeout
    CHECK(!r.complete());

    std::puts("-- pad-missing: an Always pad name that does not exist");
    P q;
    q.el("v", "videotestsrc", {{"num-buffers", "5"}}).el("k", "fakesink").link("v", "nope", "k", "sink");
    r = update(q.plan, 5000);
    CHECK_EQ(r.status, "pad-missing");
    CHECK(r.missing == std::vector<std::string>{"v.nope"});
}

static void refusedLink() {
    // A refused link starves whatever is behind it, and a sink that gets nothing never prerolls. So the
    // usual answer is "timeout" with the refusal named; "ok" needs a sink that does not wait (async=false).
    std::puts("-- refused link: a caps choice that cannot match, behind it a sink that does not wait");
    P p;
    p.el("v", "videotestsrc", {{"num-buffers", "5"}}).el("k", "fakesink", {{"async", "false"}});
    p.link("v", "src", "k", "sink", "audio/x-raw");
    UpdateResult r = update(p.plan);
    CHECK_EQ(brief(r), "ok");
    CHECK_EQ(r.refused.size(), size_t(1));
    if (!r.refused.empty()) {
        CHECK(startsWith(r.refused[0], "v.src -> k.sink: "));
        CHECK(gpstest::contains(r.refused[0], "audio/x-raw"));
    }
    const PadReport* v = pad(r, "v", "src");
    CHECK(v && !v->linked && v->temporaryEnd && startsWith(v->caps, "video/x-raw"));  // the pad is still free
    CHECK(!r.complete());

    std::puts("-- refused link: incompatible pads, no caps choice");
    P q;
    q.el("a", "audiotestsrc", {{"num-buffers", "5"}}).el("k", "fakesink", {{"async", "false"}}).el("conv", "videoconvert");
    q.link("a", "src", "conv", "sink");
    r = update(q.plan, 1500);
    CHECK_EQ(r.refused.size(), size_t(1));
    if (!r.refused.empty()) CHECK(gpstest::contains(r.refused[0], "caps do not match"));
    CHECK(!r.complete());

    std::puts("-- refused link into a sink that waits: timeout, and the message still names the refusal");
    P s;
    s.el("v", "videotestsrc", {{"num-buffers", "5"}}).el("k", "fakesink").link("v", "src", "k", "sink", "audio/x-raw");
    r = update(s.plan, 1200);
    CHECK_EQ(r.refused.size(), size_t(1));
    CHECK_EQ(r.status, "timeout");
    CHECK(gpstest::contains(r.message, "refused link"));

    std::puts("-- caps text GStreamer cannot read is a refusal too");
    P t;
    t.el("v", "videotestsrc", {{"num-buffers", "5"}}).el("k", "fakesink", {{"async", "false"}}).link("v", "src", "k", "sink", "video/x-raw, width=(int");
    r = update(t.plan);
    CHECK_EQ(r.refused.size(), size_t(1));
    if (!r.refused.empty()) CHECK(gpstest::contains(r.refused[0], "cannot be read"));

    std::puts("-- a refused Request sink is released again");
    P m;
    m.el("a", "audiotestsrc", {{"num-buffers", "5"}}).el("mix", "compositor");
    m.link("a", "src", "mix", "sink_%u");
    r = update(m.plan, 1500);
    CHECK_EQ(r.refused.size(), size_t(1));
    int mixSinks = 0;
    for (const PadReport& pr : r.pads) mixSinks += (pr.element == "mix" && !pr.isSrc);
    CHECK_EQ(mixSinks, 0);
}

static void timeoutNoQueue() {
    std::puts("-- timeout: tee straight into two sinks");
    P p;
    p.el("v", "videotestsrc", {{"num-buffers", "30"}}).el("t", "tee").req("t", "src_%u", 2);
    p.el("k1", "fakesink").el("k2", "fakesink");
    p.link("v", "src", "t", "sink").link("t", "src_%u#0", "k1", "sink").link("t", "src_%u#1", "k2", "sink");
    UpdateResult r = update(p.plan, 1500);
    CHECK_EQ(r.status, "timeout");
    CHECK(r.ms >= 1500 && r.ms < 4500);
    CHECK(!r.complete());
    CHECK(!r.pads.empty());  // the report is still there

    std::puts("-- the same with a queue per branch works");
    P q;
    q.el("v", "videotestsrc", {{"num-buffers", "30"}}).el("t", "tee").req("t", "src_%u", 2);
    q.el("q1", "queue").el("q2", "queue").el("k1", "fakesink").el("k2", "fakesink");
    q.link("v", "src", "t", "sink").link("t", "src_%u#0", "q1", "sink").link("t", "src_%u#1", "q2", "sink");
    q.link("q1", "src", "k1", "sink").link("q2", "src", "k2", "sink");
    r = update(q.plan, 5000);
    CHECK_EQ(brief(r), "ok");
    CHECK(r.complete());
}

static void unknownFactory() {
    std::puts("-- unknown factory");
    P p;
    p.el("v", "videotestsrc").el("x", "no_such_factory_xyz").link("v", "src", "x", "sink");
    UpdateResult r = update(p.plan);
    CHECK_EQ(r.status, "error");
    CHECK_EQ(r.errorElement, "x");
    CHECK(gpstest::contains(r.message, "no_such_factory_xyz"));
    CHECK(r.pads.empty());
}

static void updateTwice() {
    std::puts("-- update() twice in one process leaks no state");
    P p;
    p.el("file", "filesrc", {{"location", av()}}).el("dm", "avidemux").el("jd", "jpegdec").el("t", "tee").req("t", "src_%u", 2);
    p.link("file", "src", "dm", "sink").link("dm", "video_%u#0", "jd", "sink").link("jd", "src", "t", "sink");
    UpdateResult first = update(p.plan);
    CHECK_EQ(brief(first), "ok");
    P other;
    other.el("v", "videotestsrc", {{"num-buffers", "3"}}).el("conv", "videoconvert").link("v", "src", "conv", "sink");
    UpdateResult between = update(other.plan);
    CHECK_EQ(brief(between), "ok");
    UpdateResult second = update(p.plan);
    first.ms = second.ms = 0;
    CHECK_EQ(gps::updateJson(first), gps::updateJson(second));
    CHECK(second.pads.size() == first.pads.size() && first.pads.size() > 5);
    // the second plan's pads did not leak into the others
    CHECK(!pad(second, "conv", "src"));
    CHECK(!pad(between, "dm", "audio_%u#0"));
}

static void jsonShape() {
    std::puts("-- updateJson keys");
    P p;
    p.el("v", "videotestsrc", {{"num-buffers", "3"}}).el("conv", "videoconvert").link("v", "src", "conv", "sink");
    std::string json = gps::updateJson(update(p.plan));
    for (const char* key : {"\"status\":\"ok\"", "\"message\":\"\"", "\"errorElement\":\"\"", "\"ms\":", "\"pads\":[", "\"identity\":\"src\"",
                            "\"template\":\"src\"", "\"presence\":\"always\"", "\"isSrc\":true", "\"linked\":true", "\"temporaryEnd\":true",
                            "\"unknownData\":false", "\"allowedStructures\":[{", "\"missing\":[]", "\"refused\":[]", "\"complete\":false"}) {
        CHECK(gpstest::contains(json, key));
        if (!gpstest::contains(json, key)) std::printf("    missing key text: %s\n", key);
    }
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    tempDir();  // fixtures once, up front
    chain();
    demux();
    teeRequestSrc();
    compositor();
    freeSecondSink();
    unknownData();
    wrongLocation();
    padMissing();
    refusedLink();
    timeoutNoQueue();
    unknownFactory();
    updateTwice();
    jsonShape();
    return finish("update_test");
}
