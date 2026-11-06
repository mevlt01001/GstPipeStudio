#include "builder.h"
#include "gstcompat.h"
#include "telemetry.h"

namespace gps {
namespace {
const int kPollMs = 20;
const int kUnknownGraceMs = 500;
const int kEosWaitMs = 1500;  // after a stop request EOS gets this long to come back (the tools exit within 2 s)
double nowMs() { return g_get_monotonic_time() / 1000.0; }

// "Resource not found." plus what the debug text adds after its last line break ("No such file or directory").
std::string errorText(const GError* error, const gchar* debug) {
    std::string text = error && error->message ? error->message : "unknown problem";
    std::string detail = debug ? debug : "";
    size_t nl = detail.rfind('\n');
    if (nl != std::string::npos && nl + 1 < detail.size()) text += " (" + detail.substr(nl + 1) + ")";
    return text;
}

}
// ---- update

std::string updateJson(const UpdateResult& r) {
    JsonWriter w;
    w.beginObject().field("status", r.status).field("message", r.message).field("errorElement", r.errorElement);
    w.field("ms", r.ms);
    w.key("pads").beginArray();
    for (const PadReport& p : r.pads) {
        w.beginObject().field("element", p.element).field("pad", p.pad).field("identity", p.identity);
        w.field("template", p.templateName).field("presence", p.presence).field("isSrc", p.isSrc);
        w.field("linked", p.linked).field("temporaryEnd", p.temporaryEnd).field("unknownData", p.unknownData);
        w.field("caps", p.caps).field("allowed", p.allowed);
        w.key("allowedStructures").beginArray();
        for (const StructureInfo& s : p.allowedStructures) writeStructure(w, s);
        w.end().end();
    }
    w.end();
    w.key("missing").beginArray();
    for (const std::string& m : r.missing) w.string(m);
    w.end().key("refused").beginArray();
    for (const std::string& m : r.refused) w.string(m);
    w.end().field("complete", r.complete()).end();
    return w.str();
}

namespace {

// The element of the Graph a message came from, even when a child deep inside it spoke.
std::string planElementOf(GstObject* src, GstBin* bin) {
    std::string name = src ? GST_OBJECT_NAME(src) : "";
    GstObject* current = src ? GST_OBJECT(gst_object_ref(src)) : nullptr;
    while (current) {
        GstObject* parent = gst_object_get_parent(current);
        if (parent == GST_OBJECT(bin)) {
            name = GST_OBJECT_NAME(current);
            gst_object_unref(parent);
            gst_object_unref(current);
            break;
        }
        gst_object_unref(current);
        current = parent;
    }
    return name;
}

}  // namespace

UpdateResult update(const Graph& plan, const UpdateOptions& options, const LogFn& log) {
    double begin = nowMs();
    UpdateResult result;
    GstElement* pipeline = gst_pipeline_new("gps-update");
    {
        BuildOptions build;
        build.temporaryEnds = true;
        Builder builder(plan, GST_BIN(pipeline), build, log);
        bool built = builder.build();
        if (!built) {
            Builder::Failure f = builder.failure();
            result.status = "error";
            result.errorElement = f.element;
            result.message = f.message;
        } else {
            GstBus* bus = gst_element_get_bus(pipeline);
            auto say = [&](const std::string& line) { if (log) log(line); };
            say("PLAYING");
            if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) say("PLAYING was refused, waiting for the bus message");
            bool settled = false;
            double unknownSince = 0;  // when the last typefind error came
            while (!settled) {
                if (nowMs() - begin > options.timeoutMs) {
                    result.status = "timeout";
                    result.message = "timed out: no buffer arrived within " + std::to_string(options.timeoutMs) + " ms";
                    break;
                }
                if (options.shouldStop && options.shouldStop()) {
                    result.status = "error";
                    result.message = "stopped before the pipeline settled, because the tool was asked to stop";
                    break;
                }
                if (builder.failed()) {
                    Builder::Failure f = builder.failure();
                    result.status = "error";
                    result.errorElement = f.element;
                    result.message = f.message;
                    break;
                }
                if (!builder.missing().empty()) {
                    result.status = "pad-missing";
                    break;
                }
                // A dead end (unknown data) never prerolls, so ASYNC_DONE cannot come. After a short grace
                // time the others are taken as done, but only when every other end has really got its caps.
                if (unknownSince > 0 && nowMs() - unknownSince > kUnknownGraceMs && builder.endsHaveCaps()) {
                    say("no ASYNC_DONE expected: an end sees unknown data, the other ends are ready");
                    break;
                }
                GstMessage* msg = gst_bus_timed_pop_filtered(
                    bus, kPollMs * GST_MSECOND,
                    static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING | GST_MESSAGE_EOS | GST_MESSAGE_ASYNC_DONE));
                if (!msg) continue;
                switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ASYNC_DONE:
                    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                        say("bus: ASYNC_DONE");
                        settled = true;
                    }
                    break;
                case GST_MESSAGE_EOS:
                    say("bus: EOS");
                    settled = true;
                    break;
                case GST_MESSAGE_WARNING: {
                    GError* error = nullptr;
                    gchar* debug = nullptr;
                    gst_message_parse_warning(msg, &error, &debug);
                    say(std::string("bus: WARNING ") + GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) + ": " + errorText(error, debug));
                    g_clear_error(&error);
                    g_free(debug);
                    break;
                }
                case GST_MESSAGE_ERROR: {
                    GError* error = nullptr;
                    gchar* debug = nullptr;
                    gst_message_parse_error(msg, &error, &debug);
                    std::string text = errorText(error, debug);
                    say(std::string("bus: ERROR ") + GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) + ": " + text);
                    if (builder.markUnknownData(GST_MESSAGE_SRC(msg))) {
                        unknownSince = nowMs();
                    } else {
                        result.status = "error";
                        result.errorElement = planElementOf(GST_MESSAGE_SRC(msg), GST_BIN(pipeline));
                        result.message = text;
                        settled = true;
                    }
                    g_clear_error(&error);
                    g_free(debug);
                    break;
                }
                default:
                    break;
                }
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
            if (result.status == "ok") {
                builder.checkMissing();
                if (!builder.missing().empty()) result.status = "pad-missing";
            }
        }
        result.missing = builder.missing();
        result.refused = builder.refused();
        if (result.status == "pad-missing") result.message = "the plan needs pad " + result.missing.front() + ", but the element never created it";
        // A refused link often leaves a sink without input, and a sink without input never prerolls.
        if (result.status == "timeout" && !result.refused.empty()) result.message += "; refused link: " + result.refused.front();
        result.pads = builder.report();  // before NULL: pads and caps are gone after it
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }
    gst_object_unref(pipeline);
    result.ms = nowMs() - begin;
    return result;
}

// ---- run

std::string runJson(const RunResult& r) {
    JsonWriter w;
    w.beginObject().field("status", r.status).field("message", r.message).field("ms", r.ms).end();
    return w.str();
}

std::string telemetryJson(const TelemetrySample& sample) {
    JsonWriter w;
    w.beginObject().field("type", "sample").field("elapsedMs", sample.elapsedMs).key("elements").beginArray();
    for (const auto& item : sample.elements) {
        w.beginObject().field("element", item.element);
        w.key("ageMs"); item.ageMs < 0 ? w.null() : w.number(item.ageMs);
        w.key("queueMs"); item.queueMs < 0 ? w.null() : w.number(item.queueMs);
        w.key("processingMs"); item.processingMs < 0 ? w.null() : w.number(item.processingMs);
        w.key("pendingMs"); item.pendingMs < 0 ? w.null() : w.number(item.pendingMs);
        w.key("processingCycles").integer(static_cast<long long>(item.processingCycles));
        w.key("dropRatePercent"); item.dropRatePercent < 0 ? w.null() : w.number(item.dropRatePercent);
        w.key("qosProcessed").integer(static_cast<long long>(item.qosProcessed));
        w.key("qosDropped").integer(static_cast<long long>(item.qosDropped));
        w.key("inputBuffers").integer(static_cast<long long>(item.inputBuffers));
        w.key("outputBuffers").integer(static_cast<long long>(item.outputBuffers));
        w.end();
    }
    w.end().end();
    return w.str();
}

RunResult run(const Graph& plan, const std::map<std::string, std::string>& params, const RunOptions& options, const LogFn& log) {
    double begin = nowMs();
    RunResult result;
    auto say = [&](const std::string& line) { if (log) log(line); };
    GstElement* pipeline = gst_pipeline_new("gps-run");
    {
        BuildOptions build;
        build.params = params;
        Builder builder(plan, GST_BIN(pipeline), build, log);
        std::unique_ptr<TelemetrySampler> sampler;
        if (!builder.build()) {
            Builder::Failure f = builder.failure();
            result.status = "error";
            result.message = f.message;
        } else if (!builder.freePads().empty()) {
            result.status = "refused";
            result.message = "the plan is not complete, src pad " + builder.freePads().front() + " has no link";
        } else {
            if (options.onTelemetry) sampler = std::make_unique<TelemetrySampler>(plan, pipeline);
            GstBus* bus = gst_element_get_bus(pipeline);
            say("PLAYING");
            auto change = gst_element_set_state(pipeline, GST_STATE_PLAYING);
            if (sampler) sampler->setLive(change == GST_STATE_CHANGE_NO_PREROLL);
            double lastSample = begin;
            bool stopping = false;       // EOS was sent because of SIGINT or the timeout
            std::string stopReason;      // interrupted | timeout
            double stopSince = 0;
            bool done = false;
            while (!done) {
                if (sampler && nowMs() - lastSample >= 250) {
                    lastSample = nowMs();
                    options.onTelemetry(sampler->sample(lastSample - begin));
                }
                if (builder.failed()) {
                    result.status = "error";
                    result.message = builder.failure().message;
                    break;
                }
                if (!builder.missing().empty()) {
                    result.status = "error";
                    result.message = "the plan needs pad " + builder.missing().front() + ", but the element never created it";
                    break;
                }
                if (!builder.freePads().empty()) {  // a Sometimes pad showed up that the Graph does not link
                    result.status = "refused";
                    result.message = "a src pad appeared while running and the plan does not link it: " + builder.freePads().front();
                    break;
                }
                if (!stopping) {
                    if (options.shouldStop && options.shouldStop()) stopReason = "interrupted";
                    else if (options.timeoutMs > 0 && nowMs() - begin > options.timeoutMs) stopReason = "timeout";
                    // Still waiting for the first buffer: there is nothing to finish, and EOS into a stalled pipeline can block.
                    if (!stopReason.empty() && gst_element_get_state(pipeline, nullptr, nullptr, 0) == GST_STATE_CHANGE_ASYNC) {
                        say("stopping before the pipeline started (" + stopReason + ")");
                        result.status = stopReason;
                        break;
                    }
                    if (!stopReason.empty()) {
                        say("sending EOS (" + stopReason + ")");
                        stopping = true;
                        stopSince = nowMs();
                        gst_element_send_event(pipeline, gst_event_new_eos());
                    }
                } else if (nowMs() - stopSince > kEosWaitMs) {  // the EOS never came back
                    result.status = stopReason;
                    break;
                }
                GstMessage* msg = gst_bus_timed_pop_filtered(
                    bus, 50 * GST_MSECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING | GST_MESSAGE_EOS | GST_MESSAGE_QOS));
                if (!msg) continue;
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_QOS) {
                    if (sampler) sampler->onQos(msg);
                } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
                    result.status = stopping ? stopReason : "eos";
                    say("bus: EOS");
                    done = true;
                } else {
                    GError* error = nullptr;
                    gchar* debug = nullptr;
                    bool isError = GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR;
                    if (isError) gst_message_parse_error(msg, &error, &debug);
                    else gst_message_parse_warning(msg, &error, &debug);
                    std::string text = planElementOf(GST_MESSAGE_SRC(msg), GST_BIN(pipeline)) + ": " + errorText(error, debug);
                    say(std::string("bus: ") + (isError ? "ERROR " : "WARNING ") + text);
                    if (isError) {
                        result.status = "error";
                        // A pad that never came usually ends in a not-linked error; the missing pad is the cause.
                        result.message = builder.missing().empty() ? text : "the plan needs pad " + builder.missing().front() + ", but the element never created it";
                        done = true;
                    }
                    g_clear_error(&error);
                    g_free(debug);
                }
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
            if (sampler) options.onTelemetry(sampler->sample(nowMs() - begin));
            if (result.status == "eos" || result.status == "interrupted" || result.status == "timeout") {
                builder.checkMissing();  // it may have ended before the loop looked
                if (!builder.missing().empty()) {
                    result.status = "error";
                    result.message = "the plan needs pad " + builder.missing().front() + ", but the element never created it";
                } else if (!builder.freePads().empty()) {
                    result.status = "refused";
                    result.message = "a src pad appeared while running and the plan does not link it: " + builder.freePads().front();
                }
            }
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
        sampler.reset();
    }
    gst_object_unref(pipeline);
    result.ms = nowMs() - begin;
    return result;
}

}  // namespace gps
