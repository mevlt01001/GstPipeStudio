// builder.h – builds a Plan natively into a GstBin, and the two things done with it:
// update() (SPEC 5: temporary ends, read every caps) and run() (SPEC 6: the real thing).
// The Builder knows no element by name (the queue of the temporary end excepted, SPEC 4.4).
#pragma once

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <gst/gst.h>

#include "inspector.h"
#include "plan.h"

namespace gps {

using LogFn = std::function<void(const std::string& line)>;

// Writes one property after checking the text against the property's own spec. Returns false with a
// reason (Turkish, short) and writes nothing when the property is missing, not writable, or the text is bad.
// Used for elements and for pads.
bool setProperty(GObject* object, const std::string& property, const std::string& text, std::string* reason);

struct PadReport {
    std::string element, pad, identity, templateName, presence;  // presence: always|sometimes|request
    bool isSrc = false, linked = false, temporaryEnd = false, unknownData = false;
    std::string caps, allowed;
    std::vector<StructureInfo> allowedStructures;
};

struct UpdateOptions {
    int timeoutMs = 10000;
};

struct UpdateResult {
    std::string status = "ok";  // ok | error | timeout | pad-missing
    std::string message, errorElement;
    double ms = 0;
    std::vector<PadReport> pads;
    std::vector<std::string> missing;  // "element.identity" the Plan wants and GStreamer never made
    std::vector<std::string> refused;  // "src.pad -> sink.pad: reason"

    bool complete() const;
};
std::string updateJson(const UpdateResult& result);

struct RunOptions {
    int timeoutMs = 0;                  // 0 = no limit
    std::function<bool()> shouldStop;   // polled; true makes run() send EOS (gps-run: SIGINT)
};

struct RunResult {
    std::string status = "eos";  // eos | error | timeout | interrupted | refused
    std::string message;
    double ms = 0;
};
std::string runJson(const RunResult& result);

struct BuildOptions {
    bool temporaryEnds = false;                  // update(): free src pads get queue ! typefind ! fakesink
    bool ghostPads = false;                      // plugin: free src pads become ghost pads of the bin
    std::map<std::string, std::string> params;   // exposed parameter -> text, written after the Plan's own values
};

class Builder {
public:
    struct Failure {
        std::string element, message;
    };

    Builder(const Plan& plan, GstBin* bin, const BuildOptions& options, LogFn log);
    ~Builder();  // disconnects the signals; call it before the bin is destroyed
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

    // Creates the elements, sets the values, links what can be linked now. False: see failure().
    bool build();

    // Things the pad handlers found while GStreamer was running. All are safe to call from any thread.
    bool failed() const;
    Failure failure() const;
    std::vector<std::string> missing() const;
    std::vector<std::string> refused() const;
    std::vector<std::string> freePads() const;  // "element.identity" of src pads left without link and without end
    // The Plan links whose src pad never appeared are added to missing(). element "" = all elements.
    void checkMissing(const std::string& element = "");
    // Named (Always) pads exist from the start, so a Plan link to one that is not there is missing right away.
    void checkNamedPads();
    // True when src is the typefind of a temporary end: that pad now carries unknown data.
    bool markUnknownData(GstObject* src);
    bool endsHaveCaps() const;

    std::vector<PadReport> report() const;

private:
    struct End {
        GstElement *bin = nullptr, *typefind = nullptr, *fakesink = nullptr;
    };
    struct PadInfo {
        std::string element, identity, templateName, presence;
        GstPad* pad = nullptr;
        bool linked = false, unknownData = false;
        int end = -1;  // index into ends_
    };

    static void onPadAdded(GstElement* element, GstPad* pad, gpointer self);
    static void onNoMorePads(GstElement* element, gpointer self);
    void onSrcPad(GstElement* element, GstPad* pad);
    enum class Link { Done, Refused, Failed };
    Link link(PadInfo& info, const PlanLink& l);
    void attachEnd(PadInfo& info);
    void fail(const std::string& element, const std::string& message);
    void say(const std::string& line) const;

    Plan plan_;
    GstBin* bin_;
    BuildOptions options_;
    LogFn log_;
    gint64 started_;

    mutable std::recursive_mutex mutex_;
    std::vector<std::pair<std::string, GstElement*>> nodes_;  // plan order; each holds a reference
    std::map<std::string, GstElement*> byName_;
    std::deque<PadInfo> pads_;  // deque: a nested pad-added must not move the PadInfo a link() is holding
    std::map<GstPad*, size_t> padIndex_;
    std::vector<End> ends_;
    std::map<std::pair<std::string, std::string>, int> seen_;  // (element, template) -> pads so far
    std::set<std::string> identities_;                         // "element\tidentity" of every src pad so far
    std::vector<std::string> missing_, refused_, free_;
    bool failed_ = false;
    Failure failure_;
};

UpdateResult update(const Plan& plan, const UpdateOptions& options, const LogFn& log);
RunResult run(const Plan& plan, const std::map<std::string, std::string>& params, const RunOptions& options, const LogFn& log);

}  // namespace gps
