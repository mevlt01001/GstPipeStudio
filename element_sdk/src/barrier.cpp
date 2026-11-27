#include "gps_elem/barrier.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace gps::elem {

BarrierCoordinator::~BarrierCoordinator() {
    for (auto* port : m_ports) {
        if (port) {
            port->setWakeCallback(nullptr);
        }
    }
}

void BarrierCoordinator::setPorts(std::vector<SinkPort*> ports) {
    for (auto* port : m_ports) {
        if (port) {
            port->setWakeCallback(nullptr);
        }
    }
    m_ports = std::move(ports);
    for (auto* port : m_ports) {
        if (port) {
            port->setWakeCallback([this]() { notify(); });
        }
    }
}

void BarrierCoordinator::setPolicy(BarrierPolicy policy) {
    m_policy = std::move(policy);
}

void BarrierCoordinator::notify() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_seq.fetch_add(1, std::memory_order_release);
    }
    m_cv.notify_all();
}

void BarrierCoordinator::unblock() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unblocked.store(true, std::memory_order_release);
        m_seq.fetch_add(1, std::memory_order_release);
    }
    m_cv.notify_all();
    for (auto* port : m_ports) {
        if (port) {
            port->unblock();
        }
    }
}

void BarrierCoordinator::resetUnblock() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unblocked.store(false, std::memory_order_release);
        m_seq.fetch_add(1, std::memory_order_release);
    }
    m_cv.notify_all();
    for (auto* port : m_ports) {
        if (port) {
            port->resetUnblock();
        }
    }
}

BarrierStatus BarrierCoordinator::acquire(std::vector<std::optional<Buffer>>& outBuffers) {
    outBuffers.clear();
    outBuffers.resize(m_ports.size());
    if (m_ports.empty()) {
        return BarrierStatus::Eos;
    }

    const bool isPtsMode = std::holds_alternative<PtsWindow>(m_policy);
    const PtsWindow ptsCfg = isPtsMode ? std::get<PtsWindow>(m_policy) : PtsWindow{};
    const auto deadline = (isPtsMode && ptsCfg.timeoutNs != GST_CLOCK_TIME_NONE)
                              ? std::chrono::steady_clock::now() + std::chrono::nanoseconds(ptsCfg.timeoutNs)
                              : std::chrono::steady_clock::time_point::max();

    while (true) {
        const uint64_t seqBefore = m_seq.load(std::memory_order_acquire);

        if (m_unblocked.load(std::memory_order_acquire)) {
            return BarrierStatus::Flushing;
        }

        bool anyFlushing = false;
        bool allEosEmpty = true;
        bool allActiveHaveData = true;

        for (auto* port : m_ports) {
            if (!port) {
                continue;
            }
            if (port->isFlushing() || port->isUnblocked()) {
                anyFlushing = true;
                break;
            }
            const bool empty = port->isQueueEmpty();
            const bool eos = port->isEos();
            if (!empty || !eos) {
                allEosEmpty = false;
            }
            if (empty && !eos) {
                allActiveHaveData = false;
            }
        }

        if (anyFlushing || m_unblocked.load(std::memory_order_acquire)) {
            return BarrierStatus::Flushing;
        }
        if (allEosEmpty) {
            return BarrierStatus::Eos;
        }

        auto popAllReady = [&]() -> bool {
            for (size_t i = 0; i < m_ports.size(); ++i) {
                if (!m_ports[i]) {
                    continue;
                }
                auto buf = m_ports[i]->tryPop();
                if (buf.has_value()) {
                    outBuffers[i] = std::move(buf);
                } else if (!m_ports[i]->isEos()) {
                    outBuffers.assign(m_ports.size(), std::nullopt);
                    return false;
                }
            }
            return true;
        };

        if (allActiveHaveData) {
            if (!isPtsMode) {
                if (!popAllReady()) {
                    if (m_unblocked.load(std::memory_order_acquire)) {
                        return BarrierStatus::Flushing;
                    }
                    continue;
                }
                return BarrierStatus::Ready;
            }

            // PtsWindow alignment
            GstClockTime minPts = std::numeric_limits<GstClockTime>::max();
            GstClockTime maxPts = 0;
            bool anyValidPts = false;
            bool missingPts = false;

            for (auto* port : m_ports) {
                if (!port || port->isQueueEmpty()) {
                    continue;
                }
                auto ptsOpt = port->peekPts();
                if (!ptsOpt.has_value() || *ptsOpt == GST_CLOCK_TIME_NONE) {
                    missingPts = true;
                    break;
                }
                anyValidPts = true;
                minPts = std::min(minPts, *ptsOpt);
                maxPts = std::max(maxPts, *ptsOpt);
            }

            if (missingPts || !anyValidPts || (maxPts - minPts <= ptsCfg.toleranceNs)) {
                if (!popAllReady()) {
                    if (m_unblocked.load(std::memory_order_acquire)) {
                        return BarrierStatus::Flushing;
                    }
                    continue;
                }
                return BarrierStatus::Ready;
            }

            // Drop lagging head frames whose PTS < maxPts - toleranceNs atomically
            if (maxPts > ptsCfg.toleranceNs) {
                const GstClockTime cutoff = maxPts - ptsCfg.toleranceNs;
                for (auto* port : m_ports) {
                    if (port) {
                        port->popIfHeadBefore(cutoff);
                    }
                }
            }
            continue;
        }

        // Wait for state change
        std::unique_lock<std::mutex> lock(m_mutex);
        auto pred = [&]() {
            return m_seq.load(std::memory_order_acquire) != seqBefore ||
                   m_unblocked.load(std::memory_order_acquire);
        };
        if (deadline == std::chrono::steady_clock::time_point::max()) {
            m_cv.wait(lock, pred);
        } else {
            if (!m_cv.wait_until(lock, deadline, pred)) {
                return BarrierStatus::Timeout;
            }
        }
    }
}

}  // namespace gps::elem
