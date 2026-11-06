#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gps {
struct CapsFieldInfo {
    std::string name, type;  // type: GType name of the value, or of the list/range items
    std::string kind;        // fixed | list | range
    std::vector<std::string> values;
};

struct StructureInfo {
    std::string media;
    std::vector<std::string> features;
    std::vector<CapsFieldInfo> fields;
};

struct PadReport {
    std::string element, pad, identity, templateName, presence;  // presence: always|sometimes|request
    bool isSrc = false, linked = false, temporaryEnd = false, unknownData = false;
    std::string caps, allowed;
    std::vector<StructureInfo> allowedStructures;
};

struct UpdateResult {
    std::string status = "ok";  // ok | error | timeout | pad-missing
    std::string message, errorElement;
    double ms = 0;
    std::vector<PadReport> pads;
    std::vector<std::string> missing;  // "element.identity" the Graph wants and GStreamer never made
    std::vector<std::string> refused;  // "src.pad -> sink.pad: reason"

    bool complete() const;
};

// Measurements belong to an execution, never to the authored document.
// Missing values are represented by negative numbers, not fabricated zeros.
struct ElementTelemetry {
    std::string element;
    double ageMs = -1;          // live source: pipeline running time minus outgoing buffer running time
    double queueMs = -1;        // queue's current-level-time, when available
    bool sourceInterval = false; // source-only element: consecutive output arrivals
    double processingMs = -1;   // first sink arrival to first src departure; mean of completed cycles
    double pendingMs = -1;      // first input still waiting for an output
    std::uint64_t processingCycles = 0;
    double dropRatePercent = -1; // QoS-reported buffer drops / (processed + dropped) in this interval
    std::uint64_t qosProcessed = 0, qosDropped = 0;
    std::uint64_t inputBuffers = 0, outputBuffers = 0;
};
struct TelemetrySample {
    double elapsedMs = 0;
    std::vector<ElementTelemetry> elements;
};
} // namespace gps
