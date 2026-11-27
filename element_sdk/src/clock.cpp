#include "gps_elem/clock.h"

namespace gps::elem {

ClockSync::~ClockSync() {
    unblock();
}

GstClockReturn ClockSync::waitForPts(GstElement* elem, const GstSegment& segment, GstClockTime pts) {
    if (!m_enabled || !elem || pts == GST_CLOCK_TIME_NONE || segment.format != GST_FORMAT_TIME) {
        return GST_CLOCK_OK;
    }

    GstClockTime runningTime = gst_segment_to_running_time(&segment, GST_FORMAT_TIME, pts);
    if (runningTime == GST_CLOCK_TIME_NONE) {
        return GST_CLOCK_OK;
    }

    GstClock* clock = gst_element_get_clock(elem);
    if (!clock) {
        return GST_CLOCK_OK;
    }

    GstClockTime baseTime = gst_element_get_base_time(elem);
    if (!GST_CLOCK_TIME_IS_VALID(baseTime) || !GST_CLOCK_TIME_IS_VALID(runningTime)) {
        gst_object_unref(clock);
        return GST_CLOCK_OK;
    }
    GstClockTime targetTime = baseTime + runningTime;
    GstClockID id = gst_clock_new_single_shot_id(clock, targetTime);
    if (!id) {
        gst_object_unref(clock);
        return GST_CLOCK_ERROR;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_unblocked) {
            gst_clock_id_unref(id);
            gst_object_unref(clock);
            return GST_CLOCK_UNSCHEDULED;
        }
        m_activeId = id;
    }

    GstClockTimeDiff jitter = 0;
    GstClockReturn ret = gst_clock_id_wait(id, &jitter);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_activeId == id) {
            m_activeId = nullptr;
        }
    }

    gst_clock_id_unref(id);
    gst_object_unref(clock);
    return ret;
}

void ClockSync::unblock() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_unblocked = true;
    if (m_activeId) {
        gst_clock_id_unschedule(m_activeId);
    }
}

void ClockSync::resetUnblock() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_unblocked = false;
}

}  // namespace gps::elem
