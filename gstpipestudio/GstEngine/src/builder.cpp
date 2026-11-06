// builder.cpp – see builder.h. SPEC 5 and 6.
#include "builder.h"

#include "gst_element_creator.h"
#include "gstcompat.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace gps {
namespace {

// The temporary end of a free src pad (SPEC 5.4) names three factories; the queue is the SPEC 4.4 exception.
const char* const kTypefindFactory = "typefind";
const char* const kFakesinkFactory = "fakesink";
const char* const kFakesinkSinkPad = "sink";  // part of the temporary end, like its factory names above

std::string capsText(const GstCaps* caps) {
    return caps ? take(gst_caps_to_string(caps)) : "";
}

std::string cut(const std::string& text, size_t length = 160) {
    return text.size() <= length ? text : text.substr(0, length) + "...";
}

GstPad* requestPad(GstElement* element, const std::string& templateName, const LogFn& say) {
    return GstElementCreator::requestPad(element, templateName, say);
}

}  // namespace

// ---- property values (SPEC 5, PROPERTY VALUES)

bool setProperty(GObject* object, const std::string& property, const std::string& text, std::string* reason) {
    return GstElementCreator::setProperty(object, property, text, reason);
}

namespace {

std::string badValue(const std::string& label, const std::string& text, const std::string& reason) {
    return GstElementCreator::formatPropertyError(label, text, reason);
}

}  // namespace

// ---- Builder

Builder::Builder(const Graph& plan, GstBin* bin, const BuildOptions& options, LogFn log)
    : plan_(plan), bin_(bin), options_(options), log_(std::move(log)), started_(g_get_monotonic_time()) {}

Builder::~Builder() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& node : nodes_) {
        g_signal_handlers_disconnect_by_data(node.second, this);
        gst_object_unref(node.second);
    }
    for (PadInfo& p : pads_) gst_object_unref(p.pad);
    for (End& e : ends_) {
        gst_object_unref(e.fakesink);
        gst_object_unref(e.typefind);
        gst_object_unref(e.bin);
    }
}

void Builder::say(const std::string& line) const {
    if (!log_) return;
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%8.1f ms  ", (g_get_monotonic_time() - started_) / 1000.0);
    log_(stamp + line);
}

void Builder::fail(const std::string& element, const std::string& message) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    say("problem " + element + ": " + message);
    if (failed_) return;  // the first one is the cause, later ones follow from it
    failed_ = true;
    failure_ = {element, message};
}

bool Builder::build() {
    // 1. all elements first: a link target must exist before the first pad is handled
    for (const Element& pe : plan_.elements) {
        std::string err;
        GstElement* element = GstElementCreator::createElement(pe, &err, [this](const std::string& l) { say(l); });
        if (!element) {
            fail(pe.name, err);
            return false;
        }
        if (!gst_bin_add(bin_, element)) {  // takes the floating reference
            fail(pe.name, "unable to add " + pe.name + " to the pipeline");
            return false;
        }
        gst_object_ref(element);
        nodes_.emplace_back(pe.name, element);
        byName_[pe.name] = element;
    }

    // 2. exposed parameters override the Graph's own values
    for (const auto& param : options_.params) {
        const ExposedParam* target = nullptr;
        for (const ExposedParam& p : plan_.exposed) {
            if (p.name == param.first) target = &p;
        }
        if (!target) {
            fail("", "unknown parameter: " + param.first);
            return false;
        }
        auto it = byName_.find(target->element);
        if (it == byName_.end()) {
            fail(target->element, "parameter '" + param.first + "': the plan has no element " + target->element);
            return false;
        }
        std::string reason;
        if (!setProperty(G_OBJECT(it->second), target->property, param.second, &reason)) {
            fail(target->element, badValue(target->element + "." + target->property, param.second, reason));
            return false;
        }
        say("parameter: " + param.first + " -> " + target->element + "." + target->property + " = " + param.second);
    }

    // 3. one handler serves every kind of src pad
    for (auto& node : nodes_) {
        g_signal_connect(node.second, "pad-added", G_CALLBACK(onPadAdded), this);
        g_signal_connect(node.second, "no-more-pads", G_CALLBACK(onNoMorePads), this);
    }

    // 4. the pads that exist now: Always ones, then the Request ones the Graph asks for
    for (auto& node : nodes_) {
        std::vector<GstPad*> existing;
        GST_OBJECT_LOCK(node.second);
        for (GList* l = GST_ELEMENT_CAST(node.second)->srcpads; l; l = l->next) existing.push_back(GST_PAD(gst_object_ref(l->data)));
        GST_OBJECT_UNLOCK(node.second);
        std::reverse(existing.begin(), existing.end());  // creation order
        for (GstPad* pad : existing) {
            onSrcPad(node.second, pad);
            gst_object_unref(pad);
        }
    }
    for (const Element& pe : plan_.elements) {
        for (const auto& request : pe.requestSrc) {
            for (int i = 0; i < request.second; ++i) {
                GstPad* pad = requestPad(byName_[pe.name], request.first, [this](const std::string& l) { say(l); });
                if (!pad) {
                    fail(pe.name, pe.name + ": unable to request pad '" + request.first + "'");
                    return false;
                }
                say("request pad: " + pe.name + "." + GST_PAD_NAME(pad));
                onSrcPad(byName_[pe.name], pad);  // pad-added fired already; the handled set drops the repeat
                gst_object_unref(pad);
            }
        }
    }
    checkNamedPads();
    return !failed();
}

void Builder::onPadAdded(GstElement* element, GstPad* pad, gpointer self) {
    static_cast<Builder*>(self)->onSrcPad(element, pad);
}

void Builder::onNoMorePads(GstElement* element, gpointer self) {
    Builder* b = static_cast<Builder*>(self);
    b->say(std::string("no-more-pads: ") + GST_OBJECT_NAME(element));
    b->checkMissing(GST_OBJECT_NAME(element));
}

// The one function every src pad goes through, Always, Sometimes and Request alike (SPEC 5.3).
void Builder::onSrcPad(GstElement* element, GstPad* pad) {
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (padIndex_.count(pad)) return;

    PadInfo info;
    info.element = GST_OBJECT_NAME(element);
    GstPadTemplate* tmpl = gst_pad_get_pad_template(pad);
    std::string padName = GST_PAD_NAME(pad);
    if (tmpl) {
        info.templateName = GST_PAD_TEMPLATE_NAME_TEMPLATE(tmpl);
        GstPadPresence p = GST_PAD_TEMPLATE_PRESENCE(tmpl);
        info.presence = p == GST_PAD_ALWAYS ? "always" : p == GST_PAD_SOMETIMES ? "sometimes" : "request";
        gst_object_unref(tmpl);
    } else {
        info.templateName = padName;  // a pad without template is treated like a Sometimes pad named after itself
        info.presence = "sometimes";
    }
    if (info.presence == "always") {
        info.identity = padName;
    } else {
        int& order = seen_[{info.element, info.templateName}];
        info.identity = padIdentity(info.templateName, order++);
    }
    info.pad = GST_PAD(gst_object_ref(pad));
    identities_.insert(info.element + "\t" + info.identity);
    pads_.push_back(info);
    padIndex_[pad] = pads_.size() - 1;
    size_t index = pads_.size() - 1;
    say("src pad: " + info.element + "." + padName + " [" + info.identity + ", " + info.presence + "]");

    const Connection* wanted = nullptr;
    for (const Connection& l : plan_.links) {
        if (l.srcElement == info.element && l.srcPad == info.identity) wanted = &l;
    }
    if (wanted) {
        Link result = link(pads_[index], *wanted);
        if (result == Link::Done) {
            pads_[index].linked = true;
            gst_bin_sync_children_states(bin_);  // else a capsfilter made by the link stays in NULL
            return;
        }
        if (result == Link::Failed) return;  // fail() already told why; no end for a pad of a broken plan
        // refused: the pad stays free, so it gets an end like any other free pad
    }
    if (options_.temporaryEnds) {
        attachEnd(pads_[index]);
    } else {
        free_.push_back(info.element + "." + info.identity);
        say("free src pad: " + info.element + "." + info.identity);
        if (options_.ghostPads) {
            GstPad* ghost = gst_ghost_pad_new((info.element + "_" + padName).c_str(), pad);
            gst_pad_set_active(ghost, TRUE);
            gst_element_add_pad(GST_ELEMENT(bin_), ghost);
        }
    }
}

namespace {

// Why a link was refused, from the pads' own caps. GStreamer only answers yes or no.
std::string whyRefused(GstPad* src, GstPad* sink, const GstCaps* filter) {
    if (gst_pad_is_linked(sink)) return "the sink pad is already linked";
    GstCaps* a = gst_pad_query_caps(src, nullptr);
    GstCaps* b = gst_pad_query_caps(sink, nullptr);
    std::string why;
    if (!gst_caps_can_intersect(a, b)) {
        why = "caps do not match: [" + cut(capsText(a)) + "] and [" + cut(capsText(b)) + "]";
    } else if (filter) {
        GstCaps* both = gst_caps_intersect(a, b);
        if (!gst_caps_can_intersect(both, filter)) {
            why = "the chosen caps [" + cut(capsText(filter)) + "] do not fit, the pads accept [" + cut(capsText(both)) + "]";
        }
        gst_caps_unref(both);
    }
    gst_caps_unref(a);
    gst_caps_unref(b);
    return why.empty() ? "GStreamer refused the link" : why;
}

}  // namespace

Builder::Link Builder::link(PadInfo& info, const Connection& l) {
    std::string label = info.element + "." + info.identity + " -> " + l.sinkElement + "." + l.sinkPad;
    auto sinkIt = byName_.find(l.sinkElement);
    if (sinkIt == byName_.end()) {
        fail(info.element, label + ": the target element does not exist");
        return Link::Failed;
    }
    GstElement* sinkElement = sinkIt->second;

    // A Request template asks for a new pad right now, every other name must already exist.
    GstPadTemplate* tmpl = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(sinkElement), l.sinkPad.c_str());
    bool requested = tmpl && GST_PAD_TEMPLATE_PRESENCE(tmpl) == GST_PAD_REQUEST;
    GstPad* sinkPad = requested ? requestPad(sinkElement, l.sinkPad, [this](const std::string& line) { say(line); })
                                : gst_element_get_static_pad(sinkElement, l.sinkPad.c_str());
    if (!sinkPad) {
        refused_.push_back(label + ": " + (requested ? "unable to request the sink pad" : "the sink pad does not exist"));
        say("refused: " + refused_.back());
        return Link::Refused;
    }

    for (const auto& kv : l.sinkPadProps) {  // before linking: the pad's values can matter for negotiation
        std::string reason;
        if (!setProperty(G_OBJECT(sinkPad), kv.first, kv.second, &reason)) {
            fail(l.sinkElement, badValue(l.sinkElement + "." + l.sinkPad + "::" + kv.first, kv.second, reason));
            if (requested) gst_element_release_request_pad(sinkElement, sinkPad);
            gst_object_unref(sinkPad);
            return Link::Failed;
        }
        say("pad property: " + l.sinkElement + "." + GST_PAD_NAME(sinkPad) + "::" + kv.first + " = " + kv.second);
    }

    GstCaps* filter = l.caps.empty() ? nullptr : gst_caps_from_string(l.caps.c_str());
    Link result = Link::Done;
    if (!l.caps.empty() && !filter) {
        refused_.push_back(label + ": the caps text cannot be read: " + cut(l.caps));
        result = Link::Refused;
    } else if (!gst_element_link_pads_filtered(byName_[info.element], GST_PAD_NAME(info.pad), sinkElement, GST_PAD_NAME(sinkPad),
                                               filter)) {
        refused_.push_back(label + ": " + whyRefused(info.pad, sinkPad, filter));
        result = Link::Refused;
    }
    if (result == Link::Refused) {
        say("refused: " + refused_.back());
        if (requested) gst_element_release_request_pad(sinkElement, sinkPad);
    } else {
        say("link: " + label + (l.caps.empty() ? "" : " [" + cut(l.caps) + "]"));
    }
    if (filter) gst_caps_unref(filter);
    gst_object_unref(sinkPad);
    return result;
}

void Builder::attachEnd(PadInfo& info) {
    std::string name = "temp_" + info.element + "_" + GST_PAD_NAME(info.pad);
    GstElement* bin = gst_bin_new(name.c_str());
    GstElement* queue = gst_element_factory_make(kQueueFactory, nullptr);
    GstElement* typefind = gst_element_factory_make(kTypefindFactory, nullptr);
    GstElement* fakesink = gst_element_factory_make(kFakesinkFactory, nullptr);
    if (!queue || !typefind || !fakesink) {
        fail(info.element, "unable to build a temporary end: queue, typefind or fakesink is missing");
        return;
    }
    gst_bin_add_many(GST_BIN(bin), queue, typefind, fakesink, nullptr);
    gst_element_link_many(queue, typefind, fakesink, nullptr);
    GstPad* inner = gst_element_get_static_pad(queue, kQueueSinkPad);
    GstPad* ghost = gst_ghost_pad_new("sink", inner);
    gst_element_add_pad(bin, ghost);
    gst_object_unref(inner);

    gst_object_ref(bin);
    gst_object_ref(typefind);
    gst_object_ref(fakesink);
    ends_.push_back({bin, typefind, fakesink});
    info.end = static_cast<int>(ends_.size()) - 1;

    gst_bin_add(bin_, bin);
    if (GST_PAD_LINK_FAILED(gst_pad_link(info.pad, ghost))) {
        fail(info.element, "unable to link a temporary end to " + info.element + "." + GST_PAD_NAME(info.pad));
        return;
    }
    gst_element_sync_state_with_parent(bin);
    say("temporary end: " + name);
}

bool Builder::failed() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return failed_;
}

Builder::Failure Builder::failure() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return failure_;
}

std::vector<std::string> Builder::missing() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return missing_;
}

std::vector<std::string> Builder::refused() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return refused_;
}

std::vector<std::string> Builder::freePads() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return free_;
}

void Builder::checkMissing(const std::string& element) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const Connection& l : plan_.links) {
        if (!element.empty() && l.srcElement != element) continue;
        // At no-more-pads only numbered pads can still be on their way; a named Always pad is judged at the end.
        if (!element.empty() && !splitPadIdentity(l.srcPad, nullptr, nullptr)) continue;
        std::string label = l.srcElement + "." + l.srcPad;
        if (identities_.count(l.srcElement + "\t" + l.srcPad)) continue;
        if (std::find(missing_.begin(), missing_.end(), label) == missing_.end()) {
            missing_.push_back(label);
            say("pad did not appear: " + label);
        }
    }
}

void Builder::checkNamedPads() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const Connection& l : plan_.links) {
        if (splitPadIdentity(l.srcPad, nullptr, nullptr)) continue;  // numbered pads may still come
        std::string label = l.srcElement + "." + l.srcPad;
        if (identities_.count(l.srcElement + "\t" + l.srcPad)) continue;
        if (std::find(missing_.begin(), missing_.end(), label) == missing_.end()) {
            missing_.push_back(label);
            say("pad does not exist: " + label);
        }
    }
}

// True when every temporary end that is not a dead end has seen its caps, i.e. its fakesink got a buffer.
bool Builder::endsHaveCaps() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const PadInfo& p : pads_) {
        if (p.end < 0 || p.unknownData) continue;
        GstPad* sink = gst_element_get_static_pad(ends_[p.end].fakesink, kFakesinkSinkPad);
        GstCaps* caps = gst_pad_get_current_caps(sink);
        bool has = caps != nullptr;
        if (caps) gst_caps_unref(caps);
        gst_object_unref(sink);
        if (!has) return false;
    }
    return true;
}

bool Builder::markUnknownData(GstObject* src) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (PadInfo& p : pads_) {
        if (p.end < 0 || GST_OBJECT(ends_[p.end].typefind) != src) continue;
        p.unknownData = true;
        say("unknown data: " + p.element + "." + p.identity);
        return true;
    }
    return false;
}

std::vector<PadReport> Builder::report() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<PadReport> out;
    for (const auto& node : nodes_) {
        for (const PadInfo& info : pads_) {
            if (info.element != node.first) continue;
            PadReport r;
            r.element = info.element;
            r.pad = GST_PAD_NAME(info.pad);
            r.identity = info.identity;
            r.templateName = info.templateName;
            r.presence = info.presence;
            r.isSrc = true;
            r.linked = info.linked;
            r.temporaryEnd = info.end >= 0;
            r.unknownData = info.unknownData;
            GstPad* readFrom = info.pad;  // a free pad has no caps of its own, its end does
            if (info.end >= 0) readFrom = gst_element_get_static_pad(ends_[info.end].fakesink, kFakesinkSinkPad);
            GstCaps* caps = gst_pad_get_current_caps(readFrom);
            r.caps = capsText(caps);
            if (caps) gst_caps_unref(caps);
            if (readFrom != info.pad) gst_object_unref(readFrom);
            GstCaps* allowed = gst_pad_get_allowed_caps(info.pad);
            r.allowed = capsText(allowed);
            r.allowedStructures = structuresOf(allowed);
            if (allowed) gst_caps_unref(allowed);
            out.push_back(r);
        }
        std::vector<GstPad*> sinks;
        GST_OBJECT_LOCK(node.second);
        for (GList* l = GST_ELEMENT_CAST(node.second)->sinkpads; l; l = l->next) sinks.push_back(GST_PAD(gst_object_ref(l->data)));
        GST_OBJECT_UNLOCK(node.second);
        std::reverse(sinks.begin(), sinks.end());
        std::map<std::string, int> order;
        for (GstPad* pad : sinks) {
            PadReport r;
            r.element = node.first;
            r.pad = GST_PAD_NAME(pad);
            GstPadTemplate* tmpl = gst_pad_get_pad_template(pad);
            r.templateName = tmpl ? GST_PAD_TEMPLATE_NAME_TEMPLATE(tmpl) : r.pad;
            GstPadPresence p = tmpl ? GST_PAD_TEMPLATE_PRESENCE(tmpl) : GST_PAD_SOMETIMES;
            r.presence = p == GST_PAD_ALWAYS ? "always" : p == GST_PAD_SOMETIMES ? "sometimes" : "request";
            // Same rule as the Graph: Always sink = pad name, Request sink = template name (every link asks its own pad)
            r.identity = p == GST_PAD_ALWAYS ? r.pad : p == GST_PAD_REQUEST ? r.templateName : padIdentity(r.templateName, order[r.templateName]++);
            if (tmpl) gst_object_unref(tmpl);
            r.linked = gst_pad_is_linked(pad);
            GstCaps* caps = gst_pad_get_current_caps(pad);
            r.caps = capsText(caps);
            if (caps) gst_caps_unref(caps);
            out.push_back(r);
            gst_object_unref(pad);
        }
    }
    return out;
}

} // namespace gps
