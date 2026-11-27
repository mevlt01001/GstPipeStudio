#include "gps_elem/driver.h"
#include "gps_elem/element.h"

#include <chrono>
#include <exception>
#include <thread>
#include <utility>

namespace gps::elem {

TaskDriver::~TaskDriver() {
    stop();
    if (m_lockInit) {
        g_rec_mutex_clear(&m_taskLock);
        m_lockInit = false;
    }
}

void TaskDriver::setUnblockCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(m_wakeMutex);
    m_onUnblock = std::move(cb);
}

bool TaskDriver::start(Element* owner, std::function<void()> stepFn) {
    stop();

    m_owner = owner;
    m_stepFn = std::move(stepFn);
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_unblocking.store(false, std::memory_order_release);
    }

    if (!m_lockInit) {
        g_rec_mutex_init(&m_taskLock);
        m_lockInit = true;
    }

    GstTask* task = gst_task_new(&TaskDriver::taskFunc, this, nullptr);
    if (!task) {
        return false;
    }
    gst_task_set_lock(task, &m_taskLock);
    std::lock_guard<std::mutex> lock(m_wakeMutex);
    m_task = task;
    m_running.store(true, std::memory_order_release);
    if (!gst_task_start(task)) {
        m_running.store(false, std::memory_order_release);
        gst_object_unref(task);
        m_task = nullptr;
        return false;
    }
    return true;
}

void TaskDriver::pause() {
    std::lock_guard<std::mutex> lock(m_wakeMutex);
    if (m_task) {
        gst_task_pause(m_task);
    }
}

void TaskDriver::unblock() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        m_unblocking.store(true, std::memory_order_release);
        cb = m_onUnblock;
    }
    m_wakeCv.notify_all();
    if (cb) {
        cb();
    }
}

void TaskDriver::stop() {
    GstTask* task;
    {
        std::lock_guard<std::mutex> lock(m_wakeMutex);
        // A streaming callback can still ask to pause while it is being joined.
        task = std::exchange(m_task, nullptr);
    }
    if (task) {
        unblock();
        gst_task_stop(task);
        gst_task_join(task);
        gst_object_unref(task);
    }
    m_running.store(false, std::memory_order_release);
}

bool TaskDriver::waitFor(GstClockTime durationNs) {
    std::unique_lock<std::mutex> lock(m_wakeMutex);
    m_wakeCv.wait_for(lock, std::chrono::nanoseconds(durationNs), [&]() {
        return m_unblocking.load(std::memory_order_acquire);
    });
    return !m_unblocking.load(std::memory_order_acquire);
}

void TaskDriver::taskFunc(gpointer userData) {
    auto* self = static_cast<TaskDriver*>(userData);
    if (!self) {
        return;
    }
    if (self->m_unblocking.load(std::memory_order_acquire)) {
        self->pause();
        return;
    }
    try {
        if (self->m_stepFn) {
            self->m_stepFn();
        }
    } catch (const std::exception& e) {
        if (self->m_owner) {
            self->m_owner->reportException("TaskDriver::taskFunc", e.what());
        }
        self->pause();
    } catch (...) {
        if (self->m_owner) {
            self->m_owner->reportException("TaskDriver::taskFunc", "unknown C++ exception");
        }
        self->pause();
    }
}

}  // namespace gps::elem
