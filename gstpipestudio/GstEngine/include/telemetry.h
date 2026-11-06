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
        std::uint64_t incoming = 0, outgoing = 0;
        double ageTotal = 0;
        std::uint64_t ageCount = 0;
        double lossPercent = -1;
    };
    struct Probe {
        TelemetrySampler* owner;
        Node* node;
        GstPad* pad;
        gulong id = 0;
        bool source;
        GstSegment segment;
        bool hasSegment = false;
    };
    static void padAdded(GstElement*, GstPad*, gpointer);
    static GstPadProbeReturn onProbe(GstPad*, GstPadProbeInfo*, gpointer);
    void attach(Node& node, GstPad* pad);
    GstElement* pipeline_;
    std::map<std::string, std::unique_ptr<Node>> nodes_;
    std::vector<Node*> order_;
    std::list<std::unique_ptr<Probe>> probes_;
    std::mutex mutex_;
    std::atomic<bool> live_{false};
};
} // namespace gps
