#include "importer.h"
#include <gst/gst.h>
#include <algorithm>
#include <map>

namespace gps {
namespace {

std::string textOf(const GValue* value) {
    if (G_VALUE_HOLDS_STRING(value)) {
        const char* s = g_value_get_string(value);
        return s ? s : "";
    }
    gchar* encoded = gst_value_serialize(value);
    std::string text = encoded ? encoded : "";
    g_free(encoded);
    return text;
}

Props changedProperties(GstElement* element, std::vector<std::string>* warnings) {
    Props out;
    guint count = 0;
    GParamSpec** specs = g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &count);
    for (guint i = 0; i < count; ++i) {
        GParamSpec* spec = specs[i];
        if ((spec->flags & (G_PARAM_READABLE | G_PARAM_WRITABLE)) != (G_PARAM_READABLE | G_PARAM_WRITABLE) ||
            (spec->flags & G_PARAM_CONSTRUCT_ONLY) || spec->owner_type == GST_TYPE_OBJECT) continue;
        GValue value = G_VALUE_INIT;
        g_value_init(&value, spec->value_type);
        g_object_get_property(G_OBJECT(element), spec->name, &value);
        if (!g_param_value_defaults(spec, &value)) {
            std::string encoded = textOf(&value);
            if (encoded.empty() && !G_VALUE_HOLDS_STRING(&value)) {
                warnings->push_back(std::string(GST_ELEMENT_NAME(element)) + "." + spec->name + " could not be serialized");
            } else {
                out[spec->name] = encoded;
            }
        }
        g_value_unset(&value);
    }
    g_free(specs);
    return out;
}

std::vector<GstElement*> children(GstElement* root, std::string* error) {
    std::vector<GstElement*> out;
    if (!GST_IS_BIN(root)) { out.push_back(GST_ELEMENT(gst_object_ref(root))); return out; }
    GstIterator* iter = gst_bin_iterate_elements(GST_BIN(root));
    GValue value = G_VALUE_INIT;
    while (true) {
        GstIteratorResult step = gst_iterator_next(iter, &value);
        if (step == GST_ITERATOR_DONE) break;
        if (step == GST_ITERATOR_RESYNC) {
            for (GstElement* child : out) gst_object_unref(child);
            out.clear();
            gst_iterator_resync(iter);
            continue;
        }
        if (step != GST_ITERATOR_OK) { *error = "GStreamer could not enumerate the pipeline"; break; }
        GstElement* child = GST_ELEMENT(g_value_get_object(&value));
        out.push_back(GST_ELEMENT(gst_object_ref(child)));
        g_value_unset(&value);
        if (!error->empty()) break;
    }
    if (G_IS_VALUE(&value)) g_value_unset(&value);
    gst_iterator_free(iter);
    if (!error->empty()) { for (GstElement* child : out) gst_object_unref(child); out.clear(); }
    std::sort(out.begin(), out.end(), [](GstElement* a, GstElement* b) {
        return std::string(GST_ELEMENT_NAME(a)) < GST_ELEMENT_NAME(b);
    });
    return out;
}

std::vector<GstPad*> padsOf(GstElement* element, GstPadDirection direction) {
    std::vector<GstPad*> pads;
    GST_OBJECT_LOCK(element);
    for (GList* item = direction == GST_PAD_SRC ? element->srcpads : element->sinkpads; item; item = item->next)
        pads.push_back(GST_PAD(gst_object_ref(item->data)));
    GST_OBJECT_UNLOCK(element);
    std::sort(pads.begin(), pads.end(), [](GstPad* a, GstPad* b) { return std::string(GST_PAD_NAME(a)) < GST_PAD_NAME(b); });
    return pads;
}

std::string templateOf(GstPad* pad, GstPadPresence* presence) {
    GstPadTemplate* tmpl = gst_pad_get_pad_template(pad);
    if (!tmpl) { *presence = GST_PAD_SOMETIMES; return GST_PAD_NAME(pad); }
    *presence = GST_PAD_TEMPLATE_PRESENCE(tmpl);
    std::string name = GST_PAD_TEMPLATE_NAME_TEMPLATE(tmpl);
    gst_object_unref(tmpl);
    return name;
}

void probe(GstElement* root, int ms, std::vector<std::string>* warnings) {
    if (ms <= 0) return;
    GstBus* bus = gst_element_get_bus(root);
    if (gst_element_set_state(root, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        warnings->push_back("the short probe could not start; dynamic links may be missing");
    const gint64 deadline = g_get_monotonic_time() + static_cast<gint64>(ms) * 1000;
    while (g_get_monotonic_time() < deadline && bus) {
        gint64 remaining = deadline - g_get_monotonic_time();
        GstMessage* message = gst_bus_timed_pop_filtered(bus, std::min<gint64>(remaining * 1000, 100 * GST_MSECOND),
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (!message) continue;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError* issue = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &issue, &debug);
            warnings->push_back(std::string("probe stopped: ") + (issue ? issue->message : "unknown GStreamer error"));
            g_clear_error(&issue); g_free(debug);
        }
        gst_message_unref(message);
        break;
    }
    if (bus) gst_object_unref(bus);
}

} // namespace

bool importLaunch(const std::string& description, int probeMs, ImportResult* out, std::string* error) {
    if (!out || !error) return false;
    error->clear();
    if (description.empty()) { *error = "pipeline description is empty"; return false; }
    if (probeMs < 0 || probeMs > 10000) { *error = "probe time must be 0–10000 ms"; return false; }
    GError* parseError = nullptr;
    GstElement* root = gst_parse_launch(description.c_str(), &parseError);
    if (parseError || !root) {
        *error = parseError ? parseError->message : "GStreamer could not parse the pipeline";
        g_clear_error(&parseError);
        if (root) gst_object_unref(root);
        return false;
    }
    ImportResult result;
    result.nativeName = GST_ELEMENT_NAME(root);
    result.graph.name = result.nativeName;
    probe(root, probeMs, &result.warnings);
    auto native = children(root, error);
    if (!error->empty()) { gst_element_set_state(root, GST_STATE_NULL); gst_object_unref(root); return false; }
    std::map<GstElement*, std::string> identities;
    for (GstElement* child : native) {
        GstElementFactory* factory = gst_element_get_factory(child);
        if (!factory) { *error = "pipeline contains an explicit bin without an element factory"; break; }
        std::string factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
        std::string id = result.graph.addSource(factoryName, changedProperties(child, &result.warnings));
        identities[child] = id;
    }
    if (error->empty()) {
        for (GstElement* child : native) {
            const std::string& id = identities.at(child);
            Element* authored = result.graph.find(id);
            std::map<std::string, int> ordinals;
            for (GstPad* pad : padsOf(child, GST_PAD_SRC)) {
                GstPadPresence presence;
                std::string tmpl = templateOf(pad, &presence);
                std::string sourceId = presence == GST_PAD_ALWAYS ? std::string(GST_PAD_NAME(pad)) :
                    padIdentity(tmpl, ordinals[tmpl]++);
                if (presence == GST_PAD_REQUEST) ++authored->requestSrc[tmpl];
                GstPad* peer = gst_pad_get_peer(pad);
                if (peer) {
                    GstElement* target = gst_pad_get_parent_element(peer);
                    auto found = identities.find(target);
                    if (found != identities.end()) {
                        GstPadPresence sinkPresence;
                        std::string sinkTemplate = templateOf(peer, &sinkPresence);
                        Connection link;
                        link.srcElement = id; link.srcPad = sourceId;
                        link.sinkElement = found->second;
                        link.sinkPad = sinkPresence == GST_PAD_REQUEST ? sinkTemplate : GST_PAD_NAME(peer);
                        result.graph.links.push_back(std::move(link));
                    } else result.warnings.push_back(id + "." + GST_PAD_NAME(pad) + " links outside the imported graph");
                    if (target) gst_object_unref(target);
                    gst_object_unref(peer);
                }
                gst_object_unref(pad);
            }
        }
    }
    for (GstElement* child : native) gst_object_unref(child);
    gst_element_set_state(root, GST_STATE_NULL);
    gst_object_unref(root);
    if (!error->empty()) return false;
    Graph check;
    if (!Graph::fromConf(result.graph.toConf(), &check, error)) return false;
    *out = std::move(result);
    return true;
}

} // namespace gps
