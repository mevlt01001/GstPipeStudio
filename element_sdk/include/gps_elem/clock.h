#pragma once

#include <gst/gst.h>

#include <mutex>

namespace gps::elem {

class ClockSync {
public:
    ClockSync() = default;
    ~ClockSync();

    ClockSync(const ClockSync&) = delete;
    ClockSync& operator=(const ClockSync&) = delete;

    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    bool isEnabled() const noexcept { return m_enabled; }

    GstClockReturn waitForPts(GstElement* elem, const GstSegment& segment, GstClockTime pts);

    void unblock();
    void resetUnblock();

private:
    bool m_enabled = true;
    std::mutex m_mutex;
    GstClockID m_activeId = nullptr;
    bool m_unblocked = false;
};

}  // namespace gps::elem
