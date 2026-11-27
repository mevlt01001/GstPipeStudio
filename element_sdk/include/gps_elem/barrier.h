#pragma once

#include "gps_elem/port.h"

#include <gst/gst.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace gps::elem {

struct AllQueuesReady {};

struct PtsWindow {
    GstClockTime toleranceNs = 20 * GST_MSECOND;
    GstClockTime timeoutNs = GST_CLOCK_TIME_NONE;
};

using BarrierPolicy = std::variant<AllQueuesReady, PtsWindow>;

enum class BarrierStatus {
    Ready,
    Eos,
    Flushing,
    Timeout
};

class BarrierCoordinator {
public:
    BarrierCoordinator() = default;
    ~BarrierCoordinator();

    BarrierCoordinator(const BarrierCoordinator&) = delete;
    BarrierCoordinator& operator=(const BarrierCoordinator&) = delete;

    void setPorts(std::vector<SinkPort*> ports);
    void setPolicy(BarrierPolicy policy);

    BarrierStatus acquire(std::vector<std::optional<Buffer>>& outBuffers);

    void unblock();
    void resetUnblock();

private:
    void notify();

    std::vector<SinkPort*> m_ports;
    BarrierPolicy m_policy = AllQueuesReady{};

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<uint64_t> m_seq{0};
    std::atomic<bool> m_unblocked{false};
};

}  // namespace gps::elem
