// run_test – run() of SPEC 6: EOS, exposed parameters, refusal, stop and timeout.
#include <atomic>
#include <chrono>
#include <thread>

#include "testutil.h"

using namespace gpstest;
using gps::RunResult;

// 16x16 I420 = 384 bytes per frame
static const char* kFrameCaps = "video/x-raw, format=(string)I420, width=(int)16, height=(int)16";
static const size_t kFrameBytes = 384;

static RunResult run(const gps::Graph& plan, std::map<std::string, std::string> params = {}, gps::RunOptions options = {}) {
    Log log;
    return gps::run(plan, params, options, log.fn());
}

// videotestsrc (1000000 frames unless overridden) -> filesink; "frames" is exposed.
static P recorder(const std::string& file, const std::string& frames) {
    P p;
    p.el("src", "videotestsrc", {{"num-buffers", frames}}).el("out", "filesink", {{"location", file}});
    p.link("src", "src", "out", "sink", kFrameCaps).expose("frames", "src", "num-buffers");
    return p;
}

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    const std::string dir = tempDir();

    std::puts("-- live telemetry samples age and queue without inventing loss");
    {
        P p;
        p.el("src", "videotestsrc", {{"is-live", "true"}, {"num-buffers", "45"}})
            .el("queue", "queue")
            .el("slow", "identity", {{"sleep-time", "50000"}})
            .el("out", "fakesink", {{"sync", "false"}})
            .link("src", "src", "queue", "sink")
            .link("queue", "src", "slow", "sink")
            .link("slow", "src", "out", "sink");
        gps::RunOptions options;
        int samples = 0; bool sawAge = false, sawQueue = false, sawInput = false;
        options.onTelemetry = [&](const gps::TelemetrySample& sample) {
            ++samples;
            for (const auto& e : sample.elements) {
                if (e.element == "src" && e.ageMs >= 0) sawAge = true;
                if (e.element == "queue" && e.queueMs >= 0) sawQueue = true;
                if (e.element == "out" && e.inputBuffers > 0) sawInput = true;
            }
            CHECK(contains(gps::telemetryJson(sample), "\"lossPercent\":null"));
        };
        CHECK_EQ(run(p.plan, {}, options).status, "eos");
        CHECK(samples >= 2); CHECK(sawAge); CHECK(sawQueue); CHECK(sawInput);
    }

    std::puts("-- run reaches EOS; the plan's own value is used");
    {
        std::string file = dir + "/plain.raw";
        RunResult r = run(recorder(file, "3").plan);
        CHECK_EQ(brief(r), "eos");
        CHECK_EQ(fileSize(file), 3 * kFrameBytes);
    }

    std::puts("-- an exposed parameter overrides the property");
    {
        std::string file = dir + "/param.raw";
        RunResult r = run(recorder(file, "1000000").plan, {{"frames", "5"}});
        CHECK_EQ(brief(r), "eos");
        CHECK_EQ(fileSize(file), 5 * kFrameBytes);  // 5, not 3 and not a million
        CHECK(r.ms < 5000);
    }

    std::puts("-- a bad parameter value is an error naming element and property");
    {
        RunResult r = run(recorder(dir + "/bad.raw", "3").plan, {{"frames", "abc"}});
        CHECK_EQ(r.status, "error");
        CHECK(startsWith(r.message, "src.num-buffers: 'abc' is not valid, "));
        r = run(recorder(dir + "/bad.raw", "3").plan, {{"frames", ""}});
        CHECK_EQ(r.status, "error");
        r = run(recorder(dir + "/bad.raw", "3").plan, {{"nope", "1"}});
        CHECK_EQ(r.status, "error");
        CHECK(contains(r.message, "nope"));
    }

    std::puts("-- run refuses a plan with a free src pad");
    {
        P p;
        p.el("src", "videotestsrc", {{"num-buffers", "3"}}).el("conv", "videoconvert").link("src", "src", "conv", "sink");
        RunResult r = run(p.plan);
        CHECK_EQ(r.status, "refused");
        CHECK(contains(r.message, "conv.src"));
        P lone;
        lone.el("src", "videotestsrc", {{"num-buffers", "3"}});
        CHECK_EQ(run(lone.plan).status, "refused");
    }

    std::puts("-- run refuses a free Sometimes pad that shows up while running");
    {
        P p;
        p.el("file", "filesrc", {{"location", dir + "/av.avi"}}).el("dm", "avidemux").el("jd", "jpegdec").el("k", "fakesink");
        p.link("file", "src", "dm", "sink").link("dm", "video_%u#0", "jd", "sink").link("jd", "src", "k", "sink");
        RunResult r = run(p.plan);  // the audio pad is free
        CHECK_EQ(r.status, "refused");
        CHECK(contains(r.message, "dm.audio_%u#0"));
    }

    std::puts("-- a complete plan with a Sometimes pad runs to EOS");
    {
        P p;
        p.el("file", "filesrc", {{"location", dir + "/av.avi"}}).el("dm", "avidemux").el("jd", "jpegdec");
        p.el("k", "fakesink").el("qa", "queue").el("ka", "fakesink");
        p.link("file", "src", "dm", "sink").link("dm", "video_%u#0", "jd", "sink").link("jd", "src", "k", "sink");
        p.link("dm", "audio_%u#0", "qa", "sink").link("qa", "src", "ka", "sink");
        RunResult r = run(p.plan);
        CHECK_EQ(brief(r), "eos");
    }

    std::puts("-- error while running: the element is named");
    {
        P p;
        p.el("file", "filesrc", {{"location", dir + "/missing.avi"}}).el("k", "fakesink").link("file", "src", "k", "sink");
        RunResult r = run(p.plan);
        CHECK_EQ(r.status, "error");
        CHECK(startsWith(r.message, "file: "));
    }

    std::puts("-- pad that never appears is an error in run");
    {
        P p;
        p.el("file", "filesrc", {{"location", dir + "/video_only.avi"}}).el("dm", "avidemux").el("q", "queue").el("k", "fakesink");
        p.el("jd", "jpegdec").el("kv", "fakesink");
        p.link("file", "src", "dm", "sink").link("dm", "audio_%u#0", "q", "sink").link("q", "src", "k", "sink");
        p.link("dm", "video_%u#0", "jd", "sink").link("jd", "src", "kv", "sink");
        RunResult r = run(p.plan);
        CHECK_EQ(r.status, "error");
        CHECK(contains(r.message, "dm.audio_%u#0"));
    }

    std::puts("-- stop request (SIGINT in gps-run) sends EOS and ends as interrupted");
    {
        P p;
        p.el("src", "videotestsrc", {{"is-live", "true"}}).el("k", "fakesink").link("src", "src", "k", "sink");
        auto start = std::chrono::steady_clock::now();
        gps::RunOptions options;
        options.shouldStop = [&] { return std::chrono::steady_clock::now() - start > std::chrono::milliseconds(400); };
        RunResult r = run(p.plan, {}, options);
        CHECK_EQ(r.status, "interrupted");
        CHECK(r.ms >= 400 && r.ms < 3000);
    }

    std::puts("-- a stop request finishes the file: EOS reaches the sink");
    {
        std::string file = dir + "/live.raw";
        P p = recorder(file, "-1");
        p.plan.find("src")->props["is-live"] = "true";
        auto start = std::chrono::steady_clock::now();
        gps::RunOptions options;
        options.shouldStop = [&] { return std::chrono::steady_clock::now() - start > std::chrono::milliseconds(500); };
        RunResult r = run(p.plan, {}, options);
        CHECK_EQ(r.status, "interrupted");
        size_t size = fileSize(file);
        CHECK(size >= 3 * kFrameBytes && size % kFrameBytes == 0);  // whole frames only
    }

    std::puts("-- timeout");
    {
        P p;
        p.el("src", "videotestsrc", {{"is-live", "true"}}).el("k", "fakesink").link("src", "src", "k", "sink");
        gps::RunOptions options;
        options.timeoutMs = 600;
        RunResult r = run(p.plan, {}, options);
        CHECK_EQ(r.status, "timeout");
        CHECK(r.ms >= 600 && r.ms < 3500);
    }

    std::puts("-- runJson");
    {
        RunResult r;
        r.status = "error";
        r.message = "a \"b\"";
        r.ms = 1.5;
        CHECK_EQ(gps::runJson(r), "{\"status\":\"error\",\"message\":\"a \\\"b\\\"\",\"ms\":1.5}");
    }
    return finish("run_test");
}
