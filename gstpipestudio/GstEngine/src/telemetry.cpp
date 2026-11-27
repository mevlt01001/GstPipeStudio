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
    if (probe->source) node.hasSourcePad.store(true, std::memory_order_relaxed);
    probe->id = gst_pad_add_probe(pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM), onProbe, probe.get(), nullptr);
    std::lock_guard<std::mutex> lock(probesMutex_);
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
            std::lock_guard<std::mutex> lock(probe.segmentMutex);
            probe.segment = *segment;
            probe.hasSegment = segment->format == GST_FORMAT_TIME;
        }
        return GST_PAD_PROBE_OK;
    }
    GstBuffer* buffer = gst_pad_probe_info_get_buffer(info);
    if (!buffer) return GST_PAD_PROBE_OK;
    gint64 ageUs = -1;
    if (probe.source && self.live_.load(std::memory_order_relaxed) && GST_BUFFER_PTS_IS_VALID(buffer)) {
        GstClockTime running = GST_CLOCK_TIME_NONE;
        {
            std::lock_guard<std::mutex> lock(probe.segmentMutex);
            if (probe.hasSegment) running = gst_segment_to_running_time(&probe.segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
        }
        if (GST_CLOCK_TIME_IS_VALID(running)) {
            GstClock* clock = gst_element_get_clock(self.pipeline_);
            if (clock) {
                GstClockTime now = gst_clock_get_time(clock);
                GstClockTime base = gst_element_get_base_time(self.pipeline_);
                if (GST_CLOCK_TIME_IS_VALID(base) && now >= base) {
                    const GstClockTime ageNs = now - base >= running ? (now - base - running) : 0;
                    ageUs = static_cast<gint64>(ageNs / GST_USECOND);
                }
                gst_object_unref(clock);
            }
        }
    }
    if (probe.source) {
        probe.node->outgoing.fetch_add(1, std::memory_order_relaxed);
        if (ageUs >= 0) {
            probe.node->ageTotalUs.fetch_add(ageUs, std::memory_order_relaxed);
            probe.node->ageCount.fetch_add(1, std::memory_order_relaxed);
        }
        gint64 pending = probe.node->pendingSinceUs.exchange(0, std::memory_order_acq_rel);
        if (pending > 0) {
            const gint64 elapsedUs = std::max<gint64>(0, g_get_monotonic_time() - pending);
            probe.node->processingTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
            probe.node->processingCycles.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        probe.node->incoming.fetch_add(1, std::memory_order_relaxed);
        gint64 expected = 0;
        probe.node->pendingSinceUs.compare_exchange_strong(expected, g_get_monotonic_time(), std::memory_order_acq_rel);
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
    std::lock_guard<std::mutex> lock(node->qosMutex);
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
        item.inputBuffers = node.incoming.exchange(0, std::memory_order_relaxed);
        item.outputBuffers = node.outgoing.exchange(0, std::memory_order_relaxed);
        const std::uint64_t ages = node.ageCount.exchange(0, std::memory_order_relaxed);
        const gint64 ageUs = node.ageTotalUs.exchange(0, std::memory_order_relaxed);
        item.ageMs = ages ? (static_cast<double>(ageUs) / 1000.0) / ages : -1;
        const std::uint64_t cycles = node.processingCycles.exchange(0, std::memory_order_relaxed);
        const gint64 procUs = node.processingTotalUs.exchange(0, std::memory_order_relaxed);
        item.processingMs = cycles ? (static_cast<double>(procUs) / 1000.0) / cycles : -1;
        item.processingCycles = cycles;
        const gint64 pending = node.pendingSinceUs.load(std::memory_order_acquire);
        item.pendingMs = node.hasSourcePad.load(std::memory_order_relaxed) && pending > 0
                             ? static_cast<double>(std::max<gint64>(0, g_get_monotonic_time() - pending)) / 1000.0
                             : -1;
        {
            std::lock_guard<std::mutex> lock(node.qosMutex);
            if (node.qosUpdated) {
                item.qosProcessed = node.qosProcessed;
                item.qosDropped = node.qosDropped;
                const double total = static_cast<double>(item.qosProcessed) + item.qosDropped;
                if (total > 0) item.dropRatePercent = 100.0 * item.qosDropped / total;
                node.qosProcessed = node.qosDropped = 0;
                node.qosUpdated = false;
            }
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
