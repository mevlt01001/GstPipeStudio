#include "telemetry.h"
#include "graph.h"
#include <gst/base/gstbasesrc.h>
#include <vector>

namespace gps {
TelemetrySampler::TelemetrySampler(const Graph& graph, GstElement* pipeline) : pipeline_(pipeline) {
    for (const Element& authored : graph.elements) {
        GstElement* element = gst_bin_get_by_name(GST_BIN(pipeline), authored.name.c_str());
        if (!element) continue;
        if (GST_IS_BASE_SRC(element) && gst_base_src_is_live(GST_BASE_SRC(element))) live_.store(true);
        auto node = std::make_unique<Node>();
        node->name = authored.name;
        node->element = element;
        Node& saved = *node;
        nodes_[saved.name] = std::move(node);
        order_.push_back(&saved);
        saved.padAdded = g_signal_connect(element, "pad-added", G_CALLBACK(padAdded), this);
        std::vector<GstPad*> pads;
        GST_OBJECT_LOCK(element);
        for (GList* item = element->srcpads; item; item = item->next)
            pads.push_back(GST_PAD(gst_object_ref(item->data)));
        for (GList* item = element->sinkpads; item; item = item->next)
            pads.push_back(GST_PAD(gst_object_ref(item->data)));
        GST_OBJECT_UNLOCK(element);
        for (GstPad* pad : pads) { attach(saved, pad); gst_object_unref(pad); }
    }
}
TelemetrySampler::~TelemetrySampler() {
    for (auto& pair : nodes_) if (pair.second->padAdded) g_signal_handler_disconnect(pair.second->element, pair.second->padAdded);
    for (auto& probe : probes_) { if (probe->id) gst_pad_remove_probe(probe->pad, probe->id); gst_object_unref(probe->pad); }
    for (auto& pair : nodes_) gst_object_unref(pair.second->element);
}
void TelemetrySampler::padAdded(GstElement* element, GstPad* pad, gpointer data) {
    auto* self = static_cast<TelemetrySampler*>(data);
    auto found = self->nodes_.find(GST_ELEMENT_NAME(element));
    if (found != self->nodes_.end()) self->attach(*found->second, pad);
}
void TelemetrySampler::attach(Node& node, GstPad* pad) {
    auto probe = std::make_unique<Probe>();
    probe->owner = this; probe->node = &node; probe->pad = GST_PAD(gst_object_ref(pad));
    probe->source = GST_PAD_DIRECTION(pad) == GST_PAD_SRC;
    gst_segment_init(&probe->segment, GST_FORMAT_UNDEFINED);
    probe->id = gst_pad_add_probe(pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM), onProbe, probe.get(), nullptr);
    std::lock_guard<std::mutex> lock(mutex_);
    if (probe->source) node.hasSourcePad = true;
    else node.hasSinkPad = true;
    probes_.push_back(std::move(probe));
}
GstPadProbeReturn TelemetrySampler::onProbe(GstPad*, GstPadProbeInfo* info, gpointer data) {
    auto& probe = *static_cast<Probe*>(data);
    auto& self = *probe.owner;
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent* event = gst_pad_probe_info_get_event(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
            const GstSegment* segment = nullptr;
            gst_event_parse_segment(event, &segment);
            std::lock_guard<std::mutex> lock(self.mutex_);
            probe.segment = *segment;
            probe.hasSegment = segment->format == GST_FORMAT_TIME;
        }
        return GST_PAD_PROBE_OK;
    }
    GstBuffer* buffer = gst_pad_probe_info_get_buffer(info);
    if (!buffer) return GST_PAD_PROBE_OK;
    double ageMs = -1;
    if (probe.source && self.live_.load() && GST_BUFFER_PTS_IS_VALID(buffer)) {
        GstClockTime running = GST_CLOCK_TIME_NONE;
        {
            std::lock_guard<std::mutex> lock(self.mutex_);
            if (probe.hasSegment) running = gst_segment_to_running_time(&probe.segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
        }
        if (GST_CLOCK_TIME_IS_VALID(running)) {
            GstClock* clock = gst_element_get_clock(self.pipeline_);
            if (clock) {
                GstClockTime now = gst_clock_get_time(clock);
                GstClockTime base = gst_element_get_base_time(self.pipeline_);
                if (GST_CLOCK_TIME_IS_VALID(base) && now >= base)
                    ageMs = now - base >= running ? static_cast<double>(now - base - running) / GST_MSECOND : 0.0;
                gst_object_unref(clock);
            }
        }
    }
    std::lock_guard<std::mutex> lock(self.mutex_);
    if (probe.source) {
        ++probe.node->outgoing;
        if (!probe.node->hasSinkPad) {
            const gint64 now = g_get_monotonic_time();
            if (probe.node->lastSourceUs) {
                probe.node->processingTotalMs += static_cast<double>(now - probe.node->lastSourceUs) / 1000.0;
                ++probe.node->processingCycles;
            }
            probe.node->lastSourceUs = now;
        }
        if (ageMs >= 0) { probe.node->ageTotal += ageMs; ++probe.node->ageCount; }
        if (probe.node->pendingSinceUs) {
            probe.node->processingTotalMs += static_cast<double>(g_get_monotonic_time() - probe.node->pendingSinceUs) / 1000.0;
            ++probe.node->processingCycles;
            probe.node->pendingSinceUs = 0;
        }
    } else {
        ++probe.node->incoming;
        if (!probe.node->pendingSinceUs) probe.node->pendingSinceUs = g_get_monotonic_time();
    }
    return GST_PAD_PROBE_OK;
}
void TelemetrySampler::onQos(GstMessage* message) {
    GstObject* reporter = GST_MESSAGE_SRC(message);
    GstObject* current = GST_OBJECT(gst_object_ref(GST_MESSAGE_SRC(message)));
    Node* node = nullptr;
    while (current) {
        auto found = nodes_.find(GST_OBJECT_NAME(current));
        if (found != nodes_.end()) { node = found->second.get(); gst_object_unref(current); break; }
        GstObject* parent = gst_object_get_parent(current);
        gst_object_unref(current); current = parent;
    }
    if (!node) return;
    GstFormat format = GST_FORMAT_UNDEFINED;
    guint64 processed = G_MAXUINT64, dropped = G_MAXUINT64;
    gst_message_parse_qos_stats(message, &format, &processed, &dropped);
    if (format != GST_FORMAT_BUFFERS || processed == G_MAXUINT64 || dropped == G_MAXUINT64) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& previous = node->qosReporters[reporter];
    node->qosProcessed += processed >= previous.first ? processed - previous.first : processed;
    node->qosDropped += dropped >= previous.second ? dropped - previous.second : dropped;
    previous = {processed, dropped};
    node->qosUpdated = true;
}
TelemetrySample TelemetrySampler::sample(double elapsedMs) {
    TelemetrySample out; out.elapsedMs = elapsedMs;
    for (Node* entry : order_) {
        Node& node = *entry;
        ElementTelemetry item; item.element = node.name;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            item.sourceInterval = !node.hasSinkPad;
            item.inputBuffers = node.incoming; item.outputBuffers = node.outgoing;
            item.ageMs = node.ageCount ? node.ageTotal / node.ageCount : -1;
            item.processingMs = node.processingCycles ? node.processingTotalMs / node.processingCycles : -1;
            item.processingCycles = node.processingCycles;
            item.pendingMs = node.hasSourcePad && node.pendingSinceUs
                                 ? static_cast<double>(g_get_monotonic_time() - node.pendingSinceUs) / 1000.0 : -1;
            if (node.qosUpdated) {
                item.qosProcessed = node.qosProcessed;
                item.qosDropped = node.qosDropped;
                const double total = static_cast<double>(item.qosProcessed) + item.qosDropped;
                if (total > 0) item.dropRatePercent = 100.0 * item.qosDropped / total;
                node.qosProcessed = node.qosDropped = 0;
                node.qosUpdated = false;
            }
            node.incoming = node.outgoing = node.ageCount = 0; node.ageTotal = 0;
            node.processingCycles = 0; node.processingTotalMs = 0;
        }
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(node.element), "current-level-time")) {
            guint64 level = 0;
            g_object_get(node.element, "current-level-time", &level, nullptr);
            item.queueMs = static_cast<double>(level) / GST_MSECOND;
        }
        out.elements.push_back(std::move(item));
    }
    return out;
}
} // namespace gps
