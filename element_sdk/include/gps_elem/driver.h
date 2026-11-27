#pragma once

#include <gst/gst.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace gps::elem {

class Element;

class TaskDriver {
public:
    TaskDriver() = default;
    ~TaskDriver();

    TaskDriver(const TaskDriver&) = delete;
    TaskDriver& operator=(const TaskDriver&) = delete;

    void setLive(bool live) noexcept { m_live.store(live, std::memory_order_release); }
    bool isLive() const noexcept { return m_live.load(std::memory_order_acquire); }

    void setUnblockCallback(std::function<void()> cb);

    bool start(Element* owner, std::function<void()> stepFn);
    void pause();
    void unblock();
    void stop();

    bool isUnblocking() const noexcept { return m_unblocking.load(std::memory_order_acquire); }
    bool isRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

    // Interruptible sleep for live/timed producers; returns false if woken by unblock/stop.
    bool waitFor(GstClockTime durationNs);

private:
    static void taskFunc(gpointer userData);

    Element* m_owner = nullptr;
    GstTask* m_task = nullptr;
    GRecMutex m_taskLock{};
    bool m_lockInit = false;

    std::function<void()> m_stepFn;
    std::function<void()> m_onUnblock;

    std::mutex m_wakeMutex;
    std::condition_variable m_wakeCv;

    std::atomic<bool> m_live{false};
    std::atomic<bool> m_unblocking{false};
    std::atomic<bool> m_running{false};
};

}  // namespace gps::elem
