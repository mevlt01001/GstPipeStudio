#include "gps_elem/port.h"
#include "gps_elem/element.h"

#include <chrono>
#include <exception>
#include <utility>

namespace gps::elem {

Port::Port(Element* element, GstPad* pad, PortSpec spec)
    : m_element(element),
      m_pad(pad ? GST_PAD_CAST(gst_object_ref_sink(pad)) : nullptr),
      m_name(std::move(spec.name)),
      m_direction(spec.direction),
      m_presence(spec.presence),
      m_templateCaps(std::move(spec.templateCaps)) {}

// ============================================================================
// SinkPort
// ============================================================================

SinkPort::SinkPort(Element* element, GstPad* pad, PortSpec spec)
    : Port(element, pad, std::move(spec)),
      m_inputMode(spec.inputMode),
      m_queueCapacity(spec.queueCapacity > 0 ? spec.queueCapacity : 1),
      m_overflowPolicy(spec.overflowPolicy) {
    gst_segment_init(&m_segment, GST_FORMAT_TIME);

    gst_pad_set_element_private(m_pad, this);
    gst_pad_set_chain_function_full(m_pad, &SinkPort::trampolineChain, nullptr, nullptr);
    gst_pad_set_chain_list_function_full(m_pad, &SinkPort::trampolineChainList, nullptr, nullptr);
    gst_pad_set_event_function_full(m_pad, &SinkPort::trampolineEvent, nullptr, nullptr);
    gst_pad_set_query_function_full(m_pad, &SinkPort::trampolineQuery, nullptr, nullptr);
    gst_pad_set_activate_function_full(m_pad, &SinkPort::trampolineActivate, nullptr, nullptr);
    gst_pad_set_activatemode_function_full(m_pad, &SinkPort::trampolineActivateMode, nullptr, nullptr);
    gst_pad_set_iterate_internal_links_function_full(m_pad, &SinkPort::trampolineIterateInternalLinks, nullptr, nullptr);
}

SinkPort::~SinkPort() {
    if (m_pad) {
        gst_pad_set_element_private(m_pad, nullptr);
        gst_object_unref(m_pad);
        m_pad = nullptr;
    }
}

void SinkPort::setDirectCallback(DirectCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_directCb = std::move(cb);
}

void SinkPort::setDirectListCallback(DirectListCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_directListCb = std::move(cb);
}

GstFlowReturn SinkPort::pullRange(uint64_t offset, uint32_t size, Buffer& outBuf) {
    if (!m_pad) {
        return GST_FLOW_ERROR;
    }
    GstBuffer* raw = nullptr;
    GstFlowReturn ret = gst_pad_pull_range(m_pad, static_cast<guint64>(offset), static_cast<guint>(size), &raw);
    if (ret == GST_FLOW_OK && raw) {
        outBuf = Buffer::adopt(raw);
    }
    return ret;
}

void SinkPort::setWakeCallback(WakeCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_wakeCb = std::move(cb);
}

void SinkPort::setDefault(Buffer buf) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_defaultMailbox = std::move(buf);
}

std::optional<Buffer> SinkPort::latest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_latestMailbox.has_value()) {
        return m_latestMailbox;
    }
    return m_defaultMailbox;
}

std::optional<Buffer> SinkPort::takeNew() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasNewMailbox) {
        return std::nullopt;
    }
    m_hasNewMailbox = false;
    return m_latestMailbox;
}

bool SinkPort::hasReceived() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hasReceived;
}

bool SinkPort::waitForFirst(GstClockTime timeoutNs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto pred = [&]() {
        return m_hasReceived || m_flushing.load(std::memory_order_acquire) ||
               m_eos.load(std::memory_order_acquire) || m_unblocked.load(std::memory_order_acquire);
    };
    if (timeoutNs == GST_CLOCK_TIME_NONE) {
        m_cv.wait(lock, pred);
    } else {
        m_cv.wait_for(lock, std::chrono::nanoseconds(timeoutNs), pred);
    }
    return m_hasReceived;
}

std::optional<Buffer> SinkPort::tryPop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.empty()) {
        return std::nullopt;
    }
    Buffer b = std::move(m_queue.front());
    m_queue.pop_front();
    m_cv.notify_all();
    return b;
}

std::optional<Buffer> SinkPort::pop(GstClockTime timeoutNs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto pred = [&]() {
        return !m_queue.empty() || m_flushing.load(std::memory_order_acquire) ||
               m_eos.load(std::memory_order_acquire) || m_unblocked.load(std::memory_order_acquire);
    };
    if (timeoutNs == GST_CLOCK_TIME_NONE) {
        m_cv.wait(lock, pred);
    } else {
        if (!m_cv.wait_for(lock, std::chrono::nanoseconds(timeoutNs), pred)) {
            return std::nullopt;
        }
    }
    if (m_queue.empty()) {
        return std::nullopt;
    }
    Buffer b = std::move(m_queue.front());
    m_queue.pop_front();
    m_cv.notify_all();
    return b;
}

std::optional<GstClockTime> SinkPort::peekPts() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.empty()) {
        return std::nullopt;
    }
    return m_queue.front().pts();
}

bool SinkPort::popIfHeadBefore(GstClockTime cutoffPts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.empty()) {
        return false;
    }
    GstClockTime headPts = m_queue.front().pts();
    if (headPts == GST_CLOCK_TIME_NONE || headPts >= cutoffPts) {
        return false;
    }
    m_queue.pop_front();
    m_cv.notify_all();
    return true;
}

bool SinkPort::isQueueEmpty() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.empty();
}

size_t SinkPort::queueSize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void SinkPort::clearQueue() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_cv.notify_all();
}

std::optional<Caps> SinkPort::currentCaps() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_currentCaps.isValid()) {
        return std::nullopt;
    }
    return m_currentCaps;
}

GstSegment SinkPort::segment() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_segment;
}

void SinkPort::unblock() {
    WakeCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unblocked.store(true, std::memory_order_release);
        cb = m_wakeCb;
    }
    m_cv.notify_all();
    if (cb) {
        cb();
    }
}

void SinkPort::resetUnblock() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unblocked.store(false, std::memory_order_release);
    }
    m_cv.notify_all();
}

void SinkPort::resetState() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_latestMailbox.reset();
    m_hasReceived = false;
    m_hasNewMailbox = false;
    m_currentCaps = Caps();
    gst_segment_init(&m_segment, GST_FORMAT_TIME);
    m_hasSegment = false;
    m_flushing.store(false, std::memory_order_release);
    m_eos.store(false, std::memory_order_release);
    m_unblocked.store(false, std::memory_order_release);
    m_cv.notify_all();
}

GstFlowReturn SinkPort::handleChain(GstBuffer* rawBuf) {
    Buffer buf = Buffer::adopt(rawBuf);
    if (m_flushing.load(std::memory_order_acquire) || m_unblocked.load(std::memory_order_acquire)) {
        return GST_FLOW_FLUSHING;
    }

    if (m_inputMode == InputMode::Direct) {
        DirectCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cb = m_directCb;
        }
        if (cb) {
            return cb(std::move(buf));
        }
        return GST_FLOW_OK;
    }

    if (m_inputMode == InputMode::Mailbox) {
        WakeCallback cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latestMailbox = std::move(buf);
            m_hasReceived = true;
            m_hasNewMailbox = true;
            cb = m_wakeCb;
        }
        m_cv.notify_all();
        if (cb) {
            cb();
        }
        return GST_FLOW_OK;
    }

    // BoundedQueue
    WakeCallback cb;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_flushing.load(std::memory_order_acquire) || m_unblocked.load(std::memory_order_acquire)) {
            return GST_FLOW_FLUSHING;
        }
        if (m_queue.size() >= m_queueCapacity) {
            switch (m_overflowPolicy) {
                case OverflowPolicy::DropNewest:
                    m_droppedCount.fetch_add(1, std::memory_order_relaxed);
                    return GST_FLOW_OK;
                case OverflowPolicy::DropOldest:
                    m_queue.pop_front();
                    m_droppedCount.fetch_add(1, std::memory_order_relaxed);
                    break;
                case OverflowPolicy::Block:
                    m_cv.wait(lock, [&]() {
                        return m_queue.size() < m_queueCapacity ||
                               m_flushing.load(std::memory_order_acquire) ||
                               m_unblocked.load(std::memory_order_acquire);
                    });
                    if (m_flushing.load(std::memory_order_acquire) || m_unblocked.load(std::memory_order_acquire)) {
                        return GST_FLOW_FLUSHING;
                    }
                    break;
            }
        }
        m_queue.push_back(std::move(buf));
        cb = m_wakeCb;
    }
    m_cv.notify_all();
    if (cb) {
        cb();
    }
    return GST_FLOW_OK;
}

GstFlowReturn SinkPort::handleChainList(GstBufferList* rawList) {
    BufferList list = BufferList::adopt(rawList);
    if (m_flushing.load(std::memory_order_acquire) || m_unblocked.load(std::memory_order_acquire)) {
        return GST_FLOW_FLUSHING;
    }

    if (m_inputMode == InputMode::Direct) {
        DirectListCallback listCb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            listCb = m_directListCb;
        }
        if (listCb) {
            return listCb(std::move(list));
        }
    }

    const size_t n = list.size();
    for (size_t i = 0; i < n; ++i) {
        Buffer b = list.get(i);
        GstFlowReturn ret = handleChain(b.release());
        if (ret != GST_FLOW_OK) {
            return ret;
        }
    }
    return GST_FLOW_OK;
}

gboolean SinkPort::handleActivate(GstObject* /*parent*/) {
    if (m_pullModePreferred.load(std::memory_order_acquire)) {
        GstQuery* q = gst_query_new_scheduling();
        gboolean pullSupported = FALSE;
        if (gst_pad_peer_query(m_pad, q)) {
            pullSupported = gst_query_has_scheduling_mode(q, GST_PAD_MODE_PULL);
        }
        gst_query_unref(q);
        if (pullSupported && gst_pad_activate_mode(m_pad, GST_PAD_MODE_PULL, TRUE)) {
            return TRUE;
        }
    }
    return gst_pad_activate_mode(m_pad, GST_PAD_MODE_PUSH, TRUE);
}

gboolean SinkPort::handleActivateMode(GstPadMode mode, gboolean active) {
    m_pullActive.store(active != FALSE && mode == GST_PAD_MODE_PULL, std::memory_order_release);
    return TRUE;
}

gboolean SinkPort::handleEvent(GstEvent* event) {
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps* rawCaps = nullptr;
            gst_event_parse_caps(event, &rawCaps);
            Caps caps = Caps::borrow(rawCaps);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_currentCaps = caps;
            }
            if (m_element && !m_element->onSinkCaps(*this, caps)) {
                gst_event_unref(event);
                return FALSE;
            }
            break;
        }
        case GST_EVENT_SEGMENT: {
            GstSegment seg;
            gst_event_copy_segment(event, &seg);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_segment = seg;
                m_hasSegment = true;
            }
            break;
        }
        case GST_EVENT_FLUSH_START: {
            WakeCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_flushing.store(true, std::memory_order_release);
                cb = m_wakeCb;
            }
            m_cv.notify_all();
            if (cb) {
                cb();
            }
            break;
        }
        case GST_EVENT_FLUSH_STOP: {
            WakeCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_flushing.store(false, std::memory_order_release);
                m_eos.store(false, std::memory_order_release);
                m_queue.clear();
                gst_segment_init(&m_segment, GST_FORMAT_TIME);
                m_hasSegment = false;
                cb = m_wakeCb;
            }
            m_cv.notify_all();
            if (cb) {
                cb();
            }
            break;
        }
        case GST_EVENT_EOS: {
            WakeCallback cb;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_eos.store(true, std::memory_order_release);
                cb = m_wakeCb;
            }
            m_cv.notify_all();
            if (cb) {
                cb();
            }
            break;
        }
        case GST_EVENT_GAP: {
            GstClockTime pts = GST_CLOCK_TIME_NONE;
            GstClockTime dur = GST_CLOCK_TIME_NONE;
            gst_event_parse_gap(event, &pts, &dur);
            if (m_element && m_element->onGap(*this, pts, dur)) {
                gst_event_unref(event);
                return TRUE;
            }
            break;
        }
        default:
            break;
    }

    if (m_element) {
        return m_element->onSinkEvent(*this, event) ? TRUE : FALSE;
    }
    return gst_pad_event_default(m_pad, GST_OBJECT_CAST(m_element ? m_element->raw() : nullptr), event);
}

gboolean SinkPort::handleQuery(GstQuery* query) {
    switch (GST_QUERY_TYPE(query)) {
        case GST_QUERY_CAPS: {
            GstCaps* filter = nullptr;
            gst_query_parse_caps(query, &filter);
            Caps result = m_element ? m_element->querySinkCaps(*this, Caps::borrow(filter)) : m_templateCaps;
            gst_query_set_caps_result(query, result.get());
            return TRUE;
        }
        case GST_QUERY_ACCEPT_CAPS: {
            GstCaps* caps = nullptr;
            gst_query_parse_accept_caps(query, &caps);
            bool accepted = m_element ? m_element->acceptSinkCaps(*this, Caps::borrow(caps))
                                      : m_templateCaps.canIntersect(Caps::borrow(caps));
            gst_query_set_accept_caps_result(query, accepted ? TRUE : FALSE);
            return TRUE;
        }
        case GST_QUERY_ALLOCATION: {
            if (m_element && m_element->proposeAllocation(*this, query)) {
                return TRUE;
            }
            break;
        }
        default:
            break;
    }
    return gst_pad_query_default(m_pad, GST_OBJECT_CAST(m_element ? m_element->raw() : nullptr), query);
}

GstFlowReturn SinkPort::trampolineChain(GstPad* pad, GstObject* /*parent*/, GstBuffer* buf) {
    auto* self = static_cast<SinkPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }
    try {
        return self->handleChain(buf);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::chain", e.what());
        }
        return GST_FLOW_ERROR;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::chain", "unknown C++ exception");
        }
        return GST_FLOW_ERROR;
    }
}

GstFlowReturn SinkPort::trampolineChainList(GstPad* pad, GstObject* /*parent*/, GstBufferList* list) {
    auto* self = static_cast<SinkPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        gst_buffer_list_unref(list);
        return GST_FLOW_ERROR;
    }
    try {
        return self->handleChainList(list);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::chain_list", e.what());
        }
        return GST_FLOW_ERROR;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::chain_list", "unknown C++ exception");
        }
        return GST_FLOW_ERROR;
    }
}

gboolean SinkPort::trampolineActivate(GstPad* pad, GstObject* parent) {
    auto* self = static_cast<SinkPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return gst_pad_activate_mode(pad, GST_PAD_MODE_PUSH, TRUE);
    }
    try {
        return self->handleActivate(parent);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::activate", e.what());
        }
        return FALSE;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::activate", "unknown C++ exception");
        }
        return FALSE;
    }
}

gboolean SinkPort::trampolineActivateMode(GstPad* pad, GstObject* /*parent*/, GstPadMode mode, gboolean active) {
    auto* self = static_cast<SinkPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return TRUE;
    }
    try {
        return self->handleActivateMode(mode, active);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::activatemode", e.what());
        }
        return FALSE;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::activatemode", "unknown C++ exception");
        }
        return FALSE;
    }
}

gboolean SinkPort::trampolineEvent(GstPad* pad, GstObject* parent, GstEvent* event) {
    auto* self = static_cast<SinkPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return gst_pad_event_default(pad, parent, event);
    }
    try {
        return self->handleEvent(event);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::event", e.what());
        }
        gst_event_unref(event);
        return FALSE;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::event", "unknown C++ exception");
        }
        gst_event_unref(event);
        return FALSE;
    }
}

gboolean SinkPort::trampolineQuery(GstPad* pad, GstObject* parent, GstQuery* query) {
    auto* self = static_cast<SinkPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return gst_pad_query_default(pad, parent, query);
    }
    try {
        return self->handleQuery(query);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::query", e.what());
        }
        return FALSE;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SinkPort::query", "unknown C++ exception");
        }
        return FALSE;
    }
}

GstIterator* SinkPort::trampolineIterateInternalLinks(GstPad* pad, GstObject* parent) {
    auto* self = static_cast<SinkPort*>(gst_pad_get_element_private(pad));
    if (self && self->m_element) {
        try {
            return self->m_element->iterateInternalLinks(pad);
        } catch (...) {
            return nullptr;
        }
    }
    return gst_pad_iterate_internal_links_default(pad, parent);
}

// ============================================================================
// SrcPort
// ============================================================================

SrcPort::SrcPort(Element* element, GstPad* pad, PortSpec spec)
    : Port(element, pad, std::move(spec)) {
    gst_segment_init(&m_segment, GST_FORMAT_TIME);

    gst_pad_set_element_private(m_pad, this);
    gst_pad_set_event_function_full(m_pad, &SrcPort::trampolineEvent, nullptr, nullptr);
    gst_pad_set_query_function_full(m_pad, &SrcPort::trampolineQuery, nullptr, nullptr);
    gst_pad_set_getrange_function_full(m_pad, &SrcPort::trampolineGetRange, nullptr, nullptr);
    gst_pad_set_activatemode_function_full(m_pad, &SrcPort::trampolineActivateMode, nullptr, nullptr);
    gst_pad_set_iterate_internal_links_function_full(m_pad, &SrcPort::trampolineIterateInternalLinks, nullptr, nullptr);
}

SrcPort::~SrcPort() {
    if (m_pad) {
        gst_pad_set_element_private(m_pad, nullptr);
        gst_object_unref(m_pad);
        m_pad = nullptr;
    }
}

void SrcPort::setStreamId(std::string streamId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_streamId == streamId && m_streamStartSent) {
        return;
    }
    m_streamId = std::move(streamId);
    m_streamStartSent = false;
    ++m_streamGen;
}

void SrcPort::setCaps(const Caps& caps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingCaps = caps;
    m_capsSent = false;
    ++m_capsGen;
}

void SrcPort::setSegment(const GstSegment& segment) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_segment = segment;
    m_hasSegment = true;
    m_segmentSent = false;
    ++m_segmentGen;
}

void SrcPort::resetStickyState(bool keepStreamStart) {
    BufferPool oldPool;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!keepStreamStart) {
            m_acceptingPool.store(false, std::memory_order_release);
            m_streamStartSent = false;
            ++m_streamGen;
            oldPool = std::move(m_pool);
        } else {
            m_acceptingPool.store(true, std::memory_order_release);
        }
        m_capsSent = false;
        ++m_capsGen;
        m_segmentSent = false;
        ++m_segmentGen;
        m_eosSent.store(false, std::memory_order_release);
    }
    if (oldPool.isValid()) {
        oldPool.setActive(false);
    }
}

void SrcPort::resetSegmentState() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_segmentSent = false;
    ++m_segmentGen;
    m_eosSent.store(false, std::memory_order_release);
}

std::optional<Caps> SrcPort::currentCaps() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pendingCaps.isValid()) {
        return std::nullopt;
    }
    return m_pendingCaps;
}

bool SrcPort::ensureStickyEvents() {
    std::lock_guard<std::mutex> pushLock(m_pushMutex);

    bool sendStreamStart = false;
    std::string streamId;
    uint64_t streamGen = 0;

    bool sendCaps = false;
    Caps capsToSend;
    uint64_t capsGen = 0;

    bool sendSegment = false;
    GstSegment segToSend{};
    uint64_t segmentGen = 0;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_streamStartSent) {
            sendStreamStart = true;
            if (m_streamId.empty()) {
                if (m_element && m_element->raw()) {
                    gchar* elemName = gst_element_get_name(m_element->raw());
                    gchar* generated = g_strdup_printf("%s/%s-%p", elemName ? elemName : "elem", m_name.c_str(), static_cast<void*>(m_pad));
                    m_streamId = generated;
                    g_free(generated);
                    g_free(elemName);
                } else {
                    m_streamId = m_name;
                }
            }
            streamId = m_streamId;
            streamGen = m_streamGen;
        }
        if (m_pendingCaps.isValid() && !m_capsSent) {
            sendCaps = true;
            capsToSend = m_pendingCaps;
            capsGen = m_capsGen;
        }
        if (!m_segmentSent) {
            sendSegment = true;
            if (!m_hasSegment) {
                gst_segment_init(&m_segment, GST_FORMAT_TIME);
                m_hasSegment = true;
            }
            segToSend = m_segment;
            segmentGen = m_segmentGen;
        }
    }

    if (sendStreamStart) {
        if (gst_pad_push_event(m_pad, gst_event_new_stream_start(streamId.c_str())) == FALSE) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_streamGen == streamGen) {
            m_streamStartSent = true;
        }
    }
    if (sendCaps) {
        if (gst_pad_push_event(m_pad, gst_event_new_caps(capsToSend.get())) == FALSE) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_capsGen == capsGen) {
            m_capsSent = true;
        }
    }
    if (sendSegment) {
        if (gst_pad_push_event(m_pad, gst_event_new_segment(&segToSend)) == FALSE) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_segmentGen == segmentGen) {
            m_segmentSent = true;
        }
    }
    return true;
}

GstFlowReturn SrcPort::push(Buffer buf) {
    if (!ensureStickyEvents()) {
        GstFlowReturn ret = GST_PAD_IS_FLUSHING(m_pad) ? GST_FLOW_FLUSHING : GST_FLOW_NOT_NEGOTIATED;
        if (m_element) {
            ret = m_element->updateFlowCombiner(m_pad, ret);
        }
        return ret;
    }
    GstFlowReturn ret = gst_pad_push(m_pad, buf.release());
    if (m_element) {
        ret = m_element->updateFlowCombiner(m_pad, ret);
    }
    return ret;
}

GstFlowReturn SrcPort::pushList(BufferList list) {
    if (!ensureStickyEvents()) {
        GstFlowReturn ret = GST_PAD_IS_FLUSHING(m_pad) ? GST_FLOW_FLUSHING : GST_FLOW_NOT_NEGOTIATED;
        if (m_element) {
            ret = m_element->updateFlowCombiner(m_pad, ret);
        }
        return ret;
    }
    GstFlowReturn ret = gst_pad_push_list(m_pad, list.release());
    if (m_element) {
        ret = m_element->updateFlowCombiner(m_pad, ret);
    }
    return ret;
}

bool SrcPort::pushGap(GstClockTime pts, GstClockTime duration) {
    if (!ensureStickyEvents()) {
        return false;
    }
    return gst_pad_push_event(m_pad, gst_event_new_gap(pts, duration)) != FALSE;
}

bool SrcPort::pushTags(GstTagList* tags) {
    if (!tags) {
        return false;
    }
    if (!ensureStickyEvents()) {
        gst_tag_list_unref(tags);
        return false;
    }
    return gst_pad_push_event(m_pad, gst_event_new_tag(tags)) != FALSE;
}

bool SrcPort::negotiateAllocation() {
    Caps caps;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_acceptingPool.store(true, std::memory_order_release);
        caps = m_pendingCaps;
    }
    if (!caps.isValid()) {
        return false;
    }
    GstQuery* query = gst_query_new_allocation(caps.get(), TRUE);
    gst_pad_peer_query(m_pad, query);
    bool ok = m_element ? m_element->decideAllocation(*this, query) : false;
    gst_query_unref(query);
    return ok;
}

void SrcPort::setPool(BufferPool pool) {
    BufferPool old;
    bool deactivateOld = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_acceptingPool.store(true, std::memory_order_release);
        GstBufferPool* newRaw = pool.raw();
        old = std::move(m_pool);
        m_pool = std::move(pool);
        deactivateOld = old.isValid() && old.raw() != newRaw;
    }
    if (deactivateOld) {
        old.setActive(false);
    }
}

BufferPool SrcPort::pool() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pool;
}

Buffer SrcPort::acquireBuffer(size_t fallbackSize) {
    BufferPool p;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_acceptingPool.load(std::memory_order_acquire)) {
            return Buffer{};
        }
        p = m_pool;
    }
    if (p.isValid()) {
        if (!p.isActive()) {
            if (!m_acceptingPool.load(std::memory_order_acquire) || !p.setActive(true)) {
                return Buffer{};
            }
        }
        Buffer out;
        const GstFlowReturn ret = p.acquire(out);
        if (ret == GST_FLOW_OK && out.isValid()) {
            return out;
        }
        if (ret == GST_FLOW_FLUSHING || !m_acceptingPool.load(std::memory_order_acquire)) {
            return Buffer{};
        }
    }
    return Buffer::allocate(fallbackSize);
}

void SrcPort::setGetRangeCallback(GetRangeCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_getRangeCb = std::move(cb);
}

bool SrcPort::pushEos() {
    if (m_eosSent.load(std::memory_order_acquire)) {
        return true;
    }
    if (!ensureStickyEvents()) {
        return false;
    }
    if (m_eosSent.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }
    return gst_pad_push_event(m_pad, gst_event_new_eos()) != FALSE;
}

bool SrcPort::pushEvent(GstEvent* event) {
    if (!event) {
        return false;
    }
    const GstEventType type = GST_EVENT_TYPE(event);
    if (type != GST_EVENT_FLUSH_START && type != GST_EVENT_FLUSH_STOP &&
        GST_EVENT_IS_SERIALIZED(event) &&
        (type == GST_EVENT_TAG || !GST_EVENT_IS_STICKY(event))) {
        if (!ensureStickyEvents()) {
            gst_event_unref(event);
            return false;
        }
    }
    return gst_pad_push_event(m_pad, event) != FALSE;
}

gboolean SrcPort::handleEvent(GstEvent* event) {
    const GstEventType type = GST_EVENT_TYPE(event);
    if (type == GST_EVENT_FLUSH_STOP) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_segmentSent = false;
        ++m_segmentGen;
        m_eosSent.store(false, std::memory_order_release);
    } else if (type == GST_EVENT_SEEK && m_element) {
        gdouble rate = 1.0;
        GstFormat format = GST_FORMAT_UNDEFINED;
        GstSeekFlags flags = GST_SEEK_FLAG_NONE;
        GstSeekType startType = GST_SEEK_TYPE_NONE;
        gint64 start = 0;
        GstSeekType stopType = GST_SEEK_TYPE_NONE;
        gint64 stop = 0;
        gst_event_parse_seek(event, &rate, &format, &flags, &startType, &start, &stopType, &stop);
        if (m_element->onSeek(*this, rate, format, flags, startType, start, stopType, stop)) {
            gst_event_unref(event);
            return TRUE;
        }
    }
    if (m_element) {
        return m_element->onSrcEvent(*this, event) ? TRUE : FALSE;
    }
    return gst_pad_event_default(m_pad, GST_OBJECT_CAST(m_element ? m_element->raw() : nullptr), event);
}

gboolean SrcPort::handleQuery(GstQuery* query) {
    switch (GST_QUERY_TYPE(query)) {
        case GST_QUERY_CAPS: {
            GstCaps* filter = nullptr;
            gst_query_parse_caps(query, &filter);
            Caps result = m_element ? m_element->querySrcCaps(*this, Caps::borrow(filter)) : m_templateCaps;
            gst_query_set_caps_result(query, result.get());
            return TRUE;
        }
        case GST_QUERY_DURATION: {
            GstFormat fmt = GST_FORMAT_UNDEFINED;
            gst_query_parse_duration(query, &fmt, nullptr);
            gint64 dur = 0;
            if (m_element && m_element->queryDuration(*this, fmt, dur)) {
                gst_query_set_duration(query, fmt, dur);
                return TRUE;
            }
            break;
        }
        case GST_QUERY_POSITION: {
            GstFormat fmt = GST_FORMAT_UNDEFINED;
            gst_query_parse_position(query, &fmt, nullptr);
            gint64 pos = 0;
            if (m_element && m_element->queryPosition(*this, fmt, pos)) {
                gst_query_set_position(query, fmt, pos);
                return TRUE;
            }
            break;
        }
        case GST_QUERY_SEEKING: {
            GstFormat fmt = GST_FORMAT_UNDEFINED;
            gst_query_parse_seeking(query, &fmt, nullptr, nullptr, nullptr);
            gboolean seekable = FALSE;
            gint64 start = 0;
            gint64 end = -1;
            if (m_element && m_element->querySeeking(*this, fmt, seekable, start, end)) {
                gst_query_set_seeking(query, fmt, seekable, start, end);
                return TRUE;
            }
            break;
        }
        case GST_QUERY_SCHEDULING: {
            if (m_element && m_element->onSrcQuery(*this, query)) {
                return TRUE;
            }
            bool hasGetRange = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                hasGetRange = static_cast<bool>(m_getRangeCb);
            }
            if (hasGetRange) {
                gst_query_set_scheduling(query, GST_SCHEDULING_FLAG_SEEKABLE, 1, -1, 0);
                gst_query_add_scheduling_mode(query, GST_PAD_MODE_PULL);
                gst_query_add_scheduling_mode(query, GST_PAD_MODE_PUSH);
                return TRUE;
            }
            break;
        }
        default:
            break;
    }
    if (m_element && m_element->onSrcQuery(*this, query)) {
        return TRUE;
    }
    return gst_pad_query_default(m_pad, GST_OBJECT_CAST(m_element ? m_element->raw() : nullptr), query);
}

GstFlowReturn SrcPort::handleGetRange(uint64_t offset, uint32_t length, GstBuffer** buffer) {
    if (!buffer) {
        return GST_FLOW_ERROR;
    }
    GetRangeCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        cb = m_getRangeCb;
    }
    if (!cb) {
        return GST_FLOW_NOT_SUPPORTED;
    }
    Buffer outBuf;
    if (*buffer != nullptr) {
        outBuf = Buffer::adopt(*buffer);
        *buffer = nullptr;
    }
    GstFlowReturn ret = cb(offset, length, outBuf);
    if (ret == GST_FLOW_OK) {
        if (!outBuf.isValid()) {
            return GST_FLOW_ERROR;
        }
        *buffer = outBuf.release();
    }
    return ret;
}

gboolean SrcPort::handleActivateMode(GstPadMode mode, gboolean /*active*/) {
    if (mode == GST_PAD_MODE_PULL) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_getRangeCb ? TRUE : FALSE;
    }
    return TRUE;
}

gboolean SrcPort::trampolineEvent(GstPad* pad, GstObject* parent, GstEvent* event) {
    auto* self = static_cast<SrcPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return gst_pad_event_default(pad, parent, event);
    }
    try {
        return self->handleEvent(event);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::event", e.what());
        }
        gst_event_unref(event);
        return FALSE;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::event", "unknown C++ exception");
        }
        gst_event_unref(event);
        return FALSE;
    }
}

gboolean SrcPort::trampolineQuery(GstPad* pad, GstObject* parent, GstQuery* query) {
    auto* self = static_cast<SrcPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return gst_pad_query_default(pad, parent, query);
    }
    try {
        return self->handleQuery(query);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::query", e.what());
        }
        return FALSE;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::query", "unknown C++ exception");
        }
        return FALSE;
    }
}

GstFlowReturn SrcPort::trampolineGetRange(
    GstPad* pad,
    GstObject* /*parent*/,
    guint64 offset,
    guint length,
    GstBuffer** buffer) {
    auto* self = static_cast<SrcPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return GST_FLOW_ERROR;
    }
    try {
        return self->handleGetRange(static_cast<uint64_t>(offset), static_cast<uint32_t>(length), buffer);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::getrange", e.what());
        }
        return GST_FLOW_ERROR;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::getrange", "unknown C++ exception");
        }
        return GST_FLOW_ERROR;
    }
}

gboolean SrcPort::trampolineActivateMode(
    GstPad* pad,
    GstObject* /*parent*/,
    GstPadMode mode,
    gboolean active) {
    auto* self = static_cast<SrcPort*>(gst_pad_get_element_private(pad));
    if (!self) {
        return TRUE;
    }
    try {
        return self->handleActivateMode(mode, active);
    } catch (const std::exception& e) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::activatemode", e.what());
        }
        return FALSE;
    } catch (...) {
        if (self->m_element) {
            self->m_element->reportException("SrcPort::activatemode", "unknown C++ exception");
        }
        return FALSE;
    }
}

GstIterator* SrcPort::trampolineIterateInternalLinks(GstPad* pad, GstObject* parent) {
    auto* self = static_cast<SrcPort*>(gst_pad_get_element_private(pad));
    if (self && self->m_element) {
        try {
            return self->m_element->iterateInternalLinks(pad);
        } catch (...) {
            return nullptr;
        }
    }
    return gst_pad_iterate_internal_links_default(pad, parent);
}

}  // namespace gps::elem
