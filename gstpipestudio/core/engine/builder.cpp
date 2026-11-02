// builder.cpp – see builder.h. SPEC 5 and 6.
#include "builder.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace gps {
namespace {

// The temporary end of a free src pad (SPEC 5.4) names three factories; the queue is the SPEC 4.4 exception.
const char* const kTypefindFactory = "typefind";
const char* const kFakesinkFactory = "fakesink";
const char* const kQueueSinkPad = "sink";  // pad name of the queue, part of the queue exception
const int kPollMs = 20;
const int kUnknownGraceMs = 500;

std::string take(gchar* text) {
    std::string out = text ? text : "";
    g_free(text);
    return out;
}

std::string capsText(const GstCaps* caps) {
    return caps ? take(gst_caps_to_string(caps)) : "";
}

std::string cut(const std::string& text, size_t length = 160) {
    return text.size() <= length ? text : text.substr(0, length) + "...";
}

std::string trim(const std::string& text) {
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return text.substr(a, text.find_last_not_of(" \t\r\n") - a + 1);
}

double nowMs() { return g_get_monotonic_time() / 1000.0; }

// "Resource not found." plus what the debug text adds after its last line break ("No such file or directory").
std::string errorText(const GError* error, const gchar* debug) {
    std::string text = error && error->message ? error->message : "bilinmeyen hata";
    std::string detail = debug ? debug : "";
    size_t nl = detail.rfind('\n');
    if (nl != std::string::npos && nl + 1 < detail.size()) text += " (" + detail.substr(nl + 1) + ")";
    return text;
}

// A new Request pad from a template name ("sink_%u"). Almost every element takes the bare template; some want a
// number in the name ("sink_0"). Then the numbers are tried from 0 up, until the element gives one. No element is known.
GstPad* requestPad(GstElement* element, const std::string& templateName, const LogFn& say) {
    GstPad* pad = gst_element_request_pad_simple(element, templateName.c_str());
    if (pad) return pad;
    size_t slot = templateName.find("%u");
    if (slot == std::string::npos) slot = templateName.find("%d");
    if (slot == std::string::npos) return nullptr;
    for (int n = 0; n < 64; ++n) {
        std::string numbered = templateName;
        numbered.replace(slot, 2, std::to_string(n));
        pad = gst_element_request_pad_simple(element, numbered.c_str());
        if (pad) {
            if (say) say("istek: '" + templateName + "' kabul edilmedi, '" + numbered + "' istendi");
            return pad;
        }
    }
    return nullptr;
}

}  // namespace

// ---- property values (SPEC 5, PROPERTY VALUES)

bool setProperty(GObject* object, const std::string& property, const std::string& text, std::string* reason) {
    auto refuse = [&](const std::string& why) {
        if (reason) *reason = why;
        return false;
    };
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(object), property.c_str());
    if (!spec) return refuse("boyle bir ozellik yok");
    if (!(spec->flags & G_PARAM_WRITABLE)) return refuse("yazilamaz");
    if (spec->flags & G_PARAM_CONSTRUCT_ONLY) return refuse("yalnizca olusturulurken ayarlanir");
    if (spec->owner_type == GST_TYPE_OBJECT) return refuse("GstObject ozelligi, plan bunu degistiremez");

    // GStreamer's reader takes "" as 0 and "-5" as a huge unsigned number; both are caught by type.
    GType kind = G_TYPE_FUNDAMENTAL(spec->value_type);
    bool unsignedNumber = kind == G_TYPE_UINT || kind == G_TYPE_UINT64 || kind == G_TYPE_ULONG || kind == G_TYPE_UCHAR;
    bool needsValue = unsignedNumber || kind == G_TYPE_INT || kind == G_TYPE_INT64 || kind == G_TYPE_LONG ||
                      kind == G_TYPE_CHAR || kind == G_TYPE_FLOAT || kind == G_TYPE_DOUBLE || kind == G_TYPE_ENUM ||
                      kind == G_TYPE_BOOLEAN;
    std::string trimmed = trim(text);
    if (needsValue && trimmed.empty()) return refuse("bos metin deger olamaz");
    if (unsignedNumber && trimmed[0] == '-') return refuse("negatif sayi olamaz, isaretsiz sayi bekleniyor");

    GValue value = G_VALUE_INIT;
    g_value_init(&value, spec->value_type);
    if (!gst_value_deserialize_with_pspec(&value, text.c_str(), spec)) {
        g_value_unset(&value);
        return refuse(std::string("okunamadi, beklenen tur ") + g_type_name(spec->value_type));
    }
    if (g_param_value_validate(spec, &value)) {  // TRUE: GLib had to change it, so it was out of range
        g_value_unset(&value);
        PropertyInfo info = describeProperty(spec);
        return refuse("aralik disinda" + (info.min.empty() ? "" : ", izin verilen " + info.min + " .. " + info.max));
    }
    g_value_unset(&value);
    gst_util_set_object_arg(object, property.c_str(), text.c_str());
    return true;
}

namespace {

// "<label>: '<text>' gecersiz (<reason>)", label = "element.property"
std::string badValue(const std::string& label, const std::string& text, const std::string& reason) {
    return label + ": '" + text + "' gecersiz (" + reason + ")";
}

}  // namespace

// ---- Builder

Builder::Builder(const Plan& plan, GstBin* bin, const BuildOptions& options, LogFn log)
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
    say("HATA " + element + ": " + message);
    if (failed_) return;  // the first one is the cause, later ones follow from it
    failed_ = true;
    failure_ = {element, message};
}

bool Builder::build() {
    // 1. all elements first: a link target must exist before the first pad is handled
    for (const PlanElement& pe : plan_.elements) {
        GstElementFactory* known = gst_element_factory_find(pe.factory.c_str());
        if (known) gst_object_unref(known);
        if (!known) {
            fail(pe.name, pe.name + ": '" + pe.factory + "' diye bir element yok (eklenti kurulu degil olabilir)");
            return false;
        }
        GstElement* element = gst_element_factory_make(pe.factory.c_str(), pe.name.c_str());
        if (!element) {
            fail(pe.name, pe.name + ": '" + pe.factory + "' olusturulamadi");
            return false;
        }
        say("element: " + pe.name + " (" + pe.factory + ")");
        for (const auto& kv : pe.props) {
            std::string reason;
            if (!setProperty(G_OBJECT(element), kv.first, kv.second, &reason)) {
                fail(pe.name, badValue(pe.name + "." + kv.first, kv.second, reason));
                gst_object_unref(element);
                return false;
            }
            say("property: " + pe.name + "." + kv.first + " = " + kv.second);
        }
        if (!gst_bin_add(bin_, element)) {  // takes the floating reference
            fail(pe.name, pe.name + ": pipeline'a eklenemedi");
            return false;
        }
        gst_object_ref(element);
        nodes_.emplace_back(pe.name, element);
        byName_[pe.name] = element;
    }

    // 2. exposed parameters override the Plan's own values
    for (const auto& param : options_.params) {
        const ExposedParam* target = nullptr;
        for (const ExposedParam& p : plan_.exposed) {
            if (p.name == param.first) target = &p;
        }
        if (!target) {
            fail("", "bilinmeyen parametre: " + param.first);
            return false;
        }
        auto it = byName_.find(target->element);
        if (it == byName_.end()) {
            fail(target->element, "parametre '" + param.first + "': element yok: " + target->element);
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

    // 4. the pads that exist now: Always ones, then the Request ones the Plan asks for
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
    for (const PlanElement& pe : plan_.elements) {
        for (const auto& request : pe.requestSrc) {
            for (int i = 0; i < request.second; ++i) {
                GstPad* pad = requestPad(byName_[pe.name], request.first, [this](const std::string& l) { say(l); });
                if (!pad) {
                    fail(pe.name, pe.name + ": '" + request.first + "' pad'i istenemedi");
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

    const PlanLink* wanted = nullptr;
    for (const PlanLink& l : plan_.links) {
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
        say("bos src pad: " + info.element + "." + info.identity);
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
    if (gst_pad_is_linked(sink)) return "sink pad zaten bagli";
    GstCaps* a = gst_pad_query_caps(src, nullptr);
    GstCaps* b = gst_pad_query_caps(sink, nullptr);
    std::string why;
    if (!gst_caps_can_intersect(a, b)) {
        why = "caps uyusmuyor: [" + cut(capsText(a)) + "] ve [" + cut(capsText(b)) + "]";
    } else if (filter) {
        GstCaps* both = gst_caps_intersect(a, b);
        if (!gst_caps_can_intersect(both, filter)) {
            why = "secilen caps uyusmuyor: [" + cut(capsText(filter)) + "]; pad'ler [" + cut(capsText(both)) + "] kabul eder";
        }
        gst_caps_unref(both);
    }
    gst_caps_unref(a);
    gst_caps_unref(b);
    return why.empty() ? "baglanti reddedildi" : why;
}

}  // namespace

Builder::Link Builder::link(PadInfo& info, const PlanLink& l) {
    std::string label = info.element + "." + info.identity + " -> " + l.sinkElement + "." + l.sinkPad;
    auto sinkIt = byName_.find(l.sinkElement);
    if (sinkIt == byName_.end()) {
        fail(info.element, label + ": hedef element yok");
        return Link::Failed;
    }
    GstElement* sinkElement = sinkIt->second;

    // A Request template asks for a new pad right now, every other name must already exist.
    GstPadTemplate* tmpl = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(sinkElement), l.sinkPad.c_str());
    bool requested = tmpl && GST_PAD_TEMPLATE_PRESENCE(tmpl) == GST_PAD_REQUEST;
    GstPad* sinkPad = requested ? requestPad(sinkElement, l.sinkPad, [this](const std::string& line) { say(line); })
                                : gst_element_get_static_pad(sinkElement, l.sinkPad.c_str());
    if (!sinkPad) {
        refused_.push_back(label + ": sink pad " + (requested ? "istenemedi" : "yok"));
        say("reddedildi: " + refused_.back());
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
        refused_.push_back(label + ": caps metni okunamadi: " + cut(l.caps));
        result = Link::Refused;
    } else if (!gst_element_link_pads_filtered(byName_[info.element], GST_PAD_NAME(info.pad), sinkElement, GST_PAD_NAME(sinkPad),
                                               filter)) {
        refused_.push_back(label + ": " + whyRefused(info.pad, sinkPad, filter));
        result = Link::Refused;
    }
    if (result == Link::Refused) {
        say("reddedildi: " + refused_.back());
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
        fail(info.element, "gecici uc kurulamadi: queue, typefind veya fakesink eksik");
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
        fail(info.element, "gecici uc " + info.element + "." + GST_PAD_NAME(info.pad) + " pad'ine baglanamadi");
        return;
    }
    gst_element_sync_state_with_parent(bin);
    say("gecici uc: " + name);
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
    for (const PlanLink& l : plan_.links) {
        if (!element.empty() && l.srcElement != element) continue;
        // At no-more-pads only numbered pads can still be on their way; a named Always pad is judged at the end.
        if (!element.empty() && !splitPadIdentity(l.srcPad, nullptr, nullptr)) continue;
        std::string label = l.srcElement + "." + l.srcPad;
        if (identities_.count(l.srcElement + "\t" + l.srcPad)) continue;
        if (std::find(missing_.begin(), missing_.end(), label) == missing_.end()) {
            missing_.push_back(label);
            say("pad olusmadi: " + label);
        }
    }
}

void Builder::checkNamedPads() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const PlanLink& l : plan_.links) {
        if (splitPadIdentity(l.srcPad, nullptr, nullptr)) continue;  // numbered pads may still come
        std::string label = l.srcElement + "." + l.srcPad;
        if (identities_.count(l.srcElement + "\t" + l.srcPad)) continue;
        if (std::find(missing_.begin(), missing_.end(), label) == missing_.end()) {
            missing_.push_back(label);
            say("pad yok: " + label);
        }
    }
}

// True when every temporary end that is not a dead end has seen its caps, i.e. its fakesink got a buffer.
bool Builder::endsHaveCaps() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const PadInfo& p : pads_) {
        if (p.end < 0 || p.unknownData) continue;
        GstPad* sink = gst_element_get_static_pad(ends_[p.end].fakesink, "sink");
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
        say("bilinmeyen veri: " + p.element + "." + p.identity);
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
            if (info.end >= 0) readFrom = gst_element_get_static_pad(ends_[info.end].fakesink, "sink");
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
            // Same rule as the Plan: Always sink = pad name, Request sink = template name (every link asks its own pad)
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

// ---- update

bool UpdateResult::complete() const {
    if (status != "ok" || !missing.empty() || !refused.empty()) return false;
    for (const PadReport& p : pads) {
        if (p.isSrc && (p.temporaryEnd || p.unknownData)) return false;
    }
    return true;
}

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

// The element of the Plan a message came from, even when a child deep inside it spoke.
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

UpdateResult update(const Plan& plan, const UpdateOptions& options, const LogFn& log) {
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
            if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) say("PLAYING durum degisimi basarisiz (hata mesaji bekleniyor)");
            bool settled = false;
            double unknownSince = 0;  // when the last typefind error came
            while (!settled) {
                if (nowMs() - begin > options.timeoutMs) {
                    result.status = "timeout";
                    result.message = "zaman asimi: " + std::to_string(options.timeoutMs) + " ms icinde ilk buffer'a ulasilamadi";
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
                    say("ASYNC_DONE gelmez: bilinmeyen veri ucu var, diger uclar hazir");
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
                    say(std::string("bus: UYARI ") + GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) + ": " + errorText(error, debug));
                    g_clear_error(&error);
                    g_free(debug);
                    break;
                }
                case GST_MESSAGE_ERROR: {
                    GError* error = nullptr;
                    gchar* debug = nullptr;
                    gst_message_parse_error(msg, &error, &debug);
                    std::string text = errorText(error, debug);
                    say(std::string("bus: HATA ") + GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)) + ": " + text);
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
        if (result.status == "pad-missing") result.message = "Plan'daki pad olusmadi: " + result.missing.front();
        // A refused link often leaves a sink without input, and a sink without input never prerolls.
        if (result.status == "timeout" && !result.refused.empty()) result.message += "; reddedilen baglanti: " + result.refused.front();
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

RunResult run(const Plan& plan, const std::map<std::string, std::string>& params, const RunOptions& options, const LogFn& log) {
    double begin = nowMs();
    RunResult result;
    auto say = [&](const std::string& line) { if (log) log(line); };
    GstElement* pipeline = gst_pipeline_new("gps-run");
    {
        BuildOptions build;
        build.params = params;
        Builder builder(plan, GST_BIN(pipeline), build, log);
        if (!builder.build()) {
            Builder::Failure f = builder.failure();
            result.status = "error";
            result.message = f.message;
        } else if (!builder.freePads().empty()) {
            result.status = "refused";
            result.message = "bos src pad var, plan tamamlanmamis: " + builder.freePads().front();
        } else {
            GstBus* bus = gst_element_get_bus(pipeline);
            say("PLAYING");
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
            bool stopping = false;       // EOS was sent because of SIGINT or the timeout
            std::string stopReason;      // interrupted | timeout
            double stopSince = 0;
            bool done = false;
            while (!done) {
                if (builder.failed()) {
                    result.status = "error";
                    result.message = builder.failure().message;
                    break;
                }
                if (!builder.missing().empty()) {
                    result.status = "error";
                    result.message = "Plan'daki pad olusmadi: " + builder.missing().front();
                    break;
                }
                if (!builder.freePads().empty()) {  // a Sometimes pad showed up that the Plan does not link
                    result.status = "refused";
                    result.message = "calisirken bos src pad olustu: " + builder.freePads().front();
                    break;
                }
                if (!stopping) {
                    if (options.shouldStop && options.shouldStop()) stopReason = "interrupted";
                    else if (options.timeoutMs > 0 && nowMs() - begin > options.timeoutMs) stopReason = "timeout";
                    if (!stopReason.empty()) {
                        say("EOS gonderiliyor (" + stopReason + ")");
                        stopping = true;
                        stopSince = nowMs();
                        gst_element_send_event(pipeline, gst_event_new_eos());
                    }
                } else if (nowMs() - stopSince > 3000) {  // the EOS never came back
                    result.status = stopReason;
                    break;
                }
                GstMessage* msg = gst_bus_timed_pop_filtered(
                    bus, 50 * GST_MSECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING | GST_MESSAGE_EOS));
                if (!msg) continue;
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
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
                    say(std::string("bus: ") + (isError ? "HATA " : "UYARI ") + text);
                    if (isError) {
                        result.status = "error";
                        // A pad that never came usually ends in a not-linked error; the missing pad is the cause.
                        result.message = builder.missing().empty() ? text : "Plan'daki pad olusmadi: " + builder.missing().front();
                        done = true;
                    }
                    g_clear_error(&error);
                    g_free(debug);
                }
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
            if (result.status == "eos" || result.status == "interrupted" || result.status == "timeout") {
                builder.checkMissing();  // it may have ended before the loop looked
                if (!builder.missing().empty()) {
                    result.status = "error";
                    result.message = "Plan'daki pad olusmadi: " + builder.missing().front();
                } else if (!builder.freePads().empty()) {
                    result.status = "refused";
                    result.message = "calisirken bos src pad olustu: " + builder.freePads().front();
                }
            }
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }
    gst_object_unref(pipeline);
    result.ms = nowMs() - begin;
    return result;
}

}  // namespace gps
