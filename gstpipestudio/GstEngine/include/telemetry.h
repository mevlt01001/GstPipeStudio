#pragma once
#include "runtime.h"
#include <gst/gst.h>
#include <atomic>
#include <list>
#include <map>
#include <memory>
#include <mutex>

namespace gps {
class Graph;

// A run-scoped observer. Probes never change a buffer or the authored graph.
class TelemetrySampler final {
public:
    TelemetrySampler(const Graph& graph, GstElement* pipeline);
    ~TelemetrySampler();
    TelemetrySampler(const TelemetrySampler&) = delete;
    TelemetrySampler& operator=(const TelemetrySampler&) = delete;
    void setLive(bool live) { if (live) live_.store(true); }
    void onQos(GstMessage* message);
    TelemetrySample sample(double elapsedMs);
private:
    struct Node {
        std::string name;
        GstElement* element = nullptr;
        gulong padAdded = 0;
        std::atomic<std::uint64_t> incoming{0}, outgoing{0};
        std::atomic<gint64> ageTotalUs{0};
        std::atomic<std::uint64_t> ageCount{0};
        std::atomic<gint64> pendingSinceUs{0};
        std::atomic<bool> hasSourcePad{false};
        std::atomic<gint64> processingTotalUs{0};
        std::atomic<std::uint64_t> processingCycles{0};
        std::mutex qosMutex;
        std::map<GstObject*, std::pair<std::uint64_t, std::uint64_t>> qosReporters;
        std::uint64_t qosProcessed = 0, qosDropped = 0;
        bool qosUpdated = false;
    };
    struct Probe {
        TelemetrySampler* owner;
        Node* node;
        GstPad* pad;
        gulong id = 0;
        bool source;
        std::mutex segmentMutex;
        GstSegment segment;
        bool hasSegment = false;
    };
    static void padAdded(GstElement*, GstPad*, gpointer);
    static GstPadProbeReturn onProbe(GstPad*, GstPadProbeInfo*, gpointer);
    void attach(Node& node, GstPad* pad);
    GstElement* pipeline_;
    std::map<std::string, std::unique_ptr<Node>> nodes_;
    std::vector<Node*> order_;
    std::mutex probesMutex_;
    std::list<std::unique_ptr<Probe>> probes_;
    std::atomic<bool> live_{false};
};
} // namespace gps
