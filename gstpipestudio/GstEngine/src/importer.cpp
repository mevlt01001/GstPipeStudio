#include "importer.h"
#include "nativepad.h"
#include <gst/gst.h>
#include <algorithm>
#include <map>
#include <set>

namespace gps {
namespace {

std::string textOf(const GValue* value) {
    if (G_VALUE_HOLDS_STRING(value)) {
        const char* s = g_value_get_string(value);
        return s ? s : "";
    }
    if (GST_VALUE_HOLDS_CAPS(value)) {
        const GstCaps* caps = gst_value_get_caps(value);
        if (!caps) return "";
        gchar* text = gst_caps_to_string(caps);
        std::string out = text ? text : "";
        g_free(text);
        return out;
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

bool hasSometimesTemplates(const std::vector<GstElement*>& elements) {
    for (GstElement* el : elements) {
        GstElementFactory* factory = gst_element_get_factory(el);
        if (!factory) continue;
        for (const GList* t = gst_element_factory_get_static_pad_templates(factory); t; t = t->next) {
            const auto* tmpl = static_cast<const GstStaticPadTemplate*>(t->data);
            if (tmpl->presence == GST_PAD_SOMETIMES) return true;
        }
    }
    return false;
}

std::vector<GstElement*> topologicalOrder(const std::vector<GstElement*>& elements) {
    std::set<GstElement*> memberSet(elements.begin(), elements.end());
    std::map<GstElement*, int> inDegree;
    std::map<GstElement*, std::vector<GstElement*>> adj;
    for (GstElement* el : elements) inDegree[el] = 0;

    for (GstElement* el : elements) {
        for (GstPad* pad : padsOf(el, GST_PAD_SRC)) {
            if (GstPad* peer = gst_pad_get_peer(pad)) {
                if (GstElement* target = gst_pad_get_parent_element(peer)) {
                    if (memberSet.count(target) && target != el) {
                        adj[el].push_back(target);
                        ++inDegree[target];
                    }
                    gst_object_unref(target);
                }
                gst_object_unref(peer);
            }
            gst_object_unref(pad);
        }
    }

    auto cmpName = [](GstElement* a, GstElement* b) {
        return std::string(GST_ELEMENT_NAME(a)) < std::string(GST_ELEMENT_NAME(b));
    };
    std::vector<GstElement*> ready;
    for (GstElement* el : elements) {
        if (inDegree[el] == 0) ready.push_back(el);
    }
    std::sort(ready.begin(), ready.end(), cmpName);

    std::vector<GstElement*> ordered;
    std::set<GstElement*> placed;
    while (!ready.empty()) {
        GstElement* cur = ready.front();
        ready.erase(ready.begin());
        if (!placed.insert(cur).second) continue;
        ordered.push_back(cur);
        for (GstElement* next : adj[cur]) {
            if (--inDegree[next] == 0) {
                ready.push_back(next);
                std::sort(ready.begin(), ready.end(), cmpName);
            }
        }
    }
    for (GstElement* el : elements) {
        if (placed.insert(el).second) ordered.push_back(el);
    }
    return ordered;
}

void collapseCapsFilters(Graph& graph) {
    for (bool collapsed = true; collapsed;) {
        collapsed = false;
        for (size_t idx = 0; idx < graph.elements.size(); ++idx) {
            const Element& e = graph.elements[idx];
            if (e.factory != "capsfilter" || !e.requestSrc.empty() || !e.requestSink.empty() || e.props.size() != 1) continue;
            auto capsIt = e.props.find("caps");
            if (capsIt == e.props.end() || capsIt->second.empty()) continue;
            const std::vector<int> in = graph.linksInto(e.name);
            const std::vector<int> out = graph.linksFromElement(e.name);
            if (in.size() != 1 || out.size() != 1) continue;
            Connection& inLink = graph.links[static_cast<size_t>(in[0])];
            const Connection& outLink = graph.links[static_cast<size_t>(out[0])];
            if (!inLink.caps.empty() || !inLink.sinkPadProps.empty()) continue;

            inLink.sinkElement = outLink.sinkElement;
            inLink.sinkPad = outLink.sinkPad;
            inLink.sinkPadProps = outLink.sinkPadProps;
            inLink.caps = capsIt->second;
            graph.links.erase(graph.links.begin() + out[0]);
            graph.elements.erase(graph.elements.begin() + static_cast<long>(idx));
            collapsed = true;
            break;
        }
    }
}

} // namespace

bool importLaunch(const std::string& description, int probeMs, ImportResult* out, std::string* error) {
    if (!out || !error) return false;
    error->clear();
    const std::string normalized = normalizeLaunchText(description);
    if (normalized.empty()) { *error = "pipeline description is empty"; return false; }
    if (probeMs < 0 || probeMs > 10000) { *error = "probe time must be 0–10000 ms"; return false; }
    GError* parseError = nullptr;
    GstElement* root = gst_parse_launch(normalized.c_str(), &parseError);
    if (parseError || !root) {
        *error = parseError ? parseError->message : "GStreamer could not parse the pipeline";
        g_clear_error(&parseError);
        if (root) gst_object_unref(root);
        return false;
    }
    ImportResult result;
    result.nativeName = GST_ELEMENT_NAME(root);
    result.graph.name = result.nativeName;

    auto initial = children(root, error);
    if (!error->empty()) { gst_element_set_state(root, GST_STATE_NULL); gst_object_unref(root); return false; }

    std::map<GstElement*, Props> authoredProps;
    for (GstElement* child : initial) {
        authoredProps[child] = changedProperties(child, &result.warnings);
    }

    const bool needsProbe = probeMs > 0 && hasSometimesTemplates(initial);
    for (GstElement* child : initial) gst_object_unref(child);

    if (needsProbe) probe(root, probeMs, &result.warnings);
    auto native = topologicalOrder(children(root, error));
    if (!error->empty()) { gst_element_set_state(root, GST_STATE_NULL); gst_object_unref(root); return false; }
    std::map<GstElement*, std::string> identities;
    for (GstElement* child : native) {
        GstElementFactory* factory = gst_element_get_factory(child);
        if (!factory) { *error = "pipeline contains an explicit bin without an element factory"; break; }
        std::string factoryName = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
        auto foundProps = authoredProps.find(child);
        Props props = foundProps != authoredProps.end()
                          ? foundProps->second
                          : changedProperties(child, &result.warnings);
        std::string id = result.graph.addSource(factoryName, std::move(props));
        identities[child] = id;
    }
    if (error->empty()) {
        for (GstElement* child : native) {
            const std::string& id = identities.at(child);
            Element* authored = result.graph.find(id);
            for (GstPad* pad : padsOf(child, GST_PAD_SINK)) {
                const NativePad sink(pad);
                if (sink.presence == GST_PAD_REQUEST && isTemplateName(sink.templateName))
                    ++authored->requestSink[sink.templateName];
                gst_object_unref(pad);
            }
            std::map<std::string, int> ordinals;
            for (GstPad* pad : padsOf(child, GST_PAD_SRC)) {
                const NativePad source(pad);
                std::string sourceId = source.srcIdentity(ordinals[source.templateName]);
                if (source.presence == GST_PAD_REQUEST) ++authored->requestSrc[source.templateName];
                GstPad* peer = gst_pad_get_peer(pad);
                if (peer) {
                    GstElement* target = gst_pad_get_parent_element(peer);
                    auto found = identities.find(target);
                    if (found != identities.end()) {
                        Connection link;
                        link.srcElement = id; link.srcPad = sourceId;
                        link.sinkElement = found->second;
                        link.sinkPad = NativePad(peer).sinkIdentity();
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
    collapseCapsFilters(result.graph);
    Graph check;
    if (!Graph::fromConf(result.graph.toConf(), &check, error)) return false;
    *out = std::move(result);
    return true;
}

} // namespace gps
