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

    m_task = gst_task_new(&TaskDriver::taskFunc, this, nullptr);
    if (!m_task) {
        return false;
    }
    gst_task_set_lock(m_task, &m_taskLock);
    m_running.store(true, std::memory_order_release);
    if (!gst_task_start(m_task)) {
        m_running.store(false, std::memory_order_release);
        gst_object_unref(m_task);
        m_task = nullptr;
        return false;
    }
    return true;
}

void TaskDriver::pause() {
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
    if (m_task) {
        unblock();
        gst_task_stop(m_task);
        gst_task_join(m_task);
        gst_object_unref(m_task);
        m_task = nullptr;
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
        if (self->m_task) {
            gst_task_pause(self->m_task);
        }
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
        if (self->m_task && !self->m_unblocking.load(std::memory_order_acquire)) {
            gst_task_pause(self->m_task);
        }
    } catch (...) {
        if (self->m_owner) {
            self->m_owner->reportException("TaskDriver::taskFunc", "unknown C++ exception");
        }
        if (self->m_task && !self->m_unblocking.load(std::memory_order_acquire)) {
            gst_task_pause(self->m_task);
        }
    }
}

}  // namespace gps::elem
