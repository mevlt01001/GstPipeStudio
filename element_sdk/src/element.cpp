#include "gps_elem/element.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace gps::elem {

namespace {

GstPadPresence toGstPresence(Presence p) {
    switch (p) {
        case Presence::Always:
            return GST_PAD_ALWAYS;
        case Presence::Request:
            return GST_PAD_REQUEST;
        case Presence::Sometimes:
            return GST_PAD_SOMETIMES;
    }
    return GST_PAD_ALWAYS;
}

}  // namespace

Element::Element(GstElement* raw)
    : m_raw(raw),
      m_flowCombiner(gst_flow_combiner_new()) {}

Element::~Element() {
    {
        std::lock_guard<std::mutex> lock(m_portsMutex);
        m_sinkPorts.clear();
        m_srcPorts.clear();
        m_internalLinks.clear();
    }
    if (m_flowCombiner) {
        gst_flow_combiner_free(m_flowCombiner);
        m_flowCombiner = nullptr;
    }
}

bool Element::onSinkCaps(SinkPort& /*port*/, const Caps& caps) {
    // Default: forward caps to src ports if fixed (never hold m_portsMutex across virtual hooks)
    if (caps.isFixed()) {
        std::vector<SrcPort*> srcs = allSrcPorts();
        for (auto* src : srcs) {
            Caps outCaps = transformCaps(GST_PAD_SINK, caps, Caps());
            if (outCaps.isValid() && !outCaps.isEmpty()) {
                if (!outCaps.isFixed()) {
                    outCaps = outCaps.fixate();
                }
                src->setCaps(outCaps);
            }
        }
    }
    return true;
}

Caps Element::transformCaps(GstPadDirection /*dir*/, const Caps& caps, const Caps& filter) {
    if (!caps.isValid()) {
        return filter.isValid() ? filter : Caps::any();
    }
    if (filter.isValid()) {
        return caps.intersect(filter);
    }
    return caps;
}

bool Element::onSinkEvent(SinkPort& port, GstEvent* event) {
    const GstEventType type = GST_EVENT_TYPE(event);
    if (type == GST_EVENT_FLUSH_STOP) {
        std::vector<SrcPort*> targets;
        {
            std::lock_guard<std::mutex> lock(m_portsMutex);
            auto it = m_internalLinks.find(port.pad());
            if (it != m_internalLinks.end() && !it->second.empty()) {
                for (GstPad* p : it->second) {
                    if (p && GST_PAD_DIRECTION(p) == GST_PAD_SRC) {
                        if (auto* src = static_cast<SrcPort*>(gst_pad_get_element_private(p))) {
                            targets.push_back(src);
                        }
                    }
                }
            } else {
                for (const auto& [name, ptr] : m_srcPorts) {
                    targets.push_back(ptr.get());
                }
            }
        }
        for (auto* src : targets) {
            src->resetSegmentState();
        }
    }
    if (port.inputMode() != InputMode::Direct) {
        if (type == GST_EVENT_EOS || type == GST_EVENT_SEGMENT ||
            type == GST_EVENT_CAPS || type == GST_EVENT_STREAM_START) {
            gst_event_unref(event);
            return true;
        }
    }
    // Default for Direct mode: forward EOS to all src ports
    if (type == GST_EVENT_EOS) {
        std::vector<SrcPort*> srcs = allSrcPorts();
        if (!srcs.empty()) {
            gst_event_unref(event);
            for (auto* src : srcs) {
                src->pushEos();
            }
            return true;
        }
    }
    return gst_pad_event_default(port.pad(), GST_OBJECT_CAST(m_raw), event) != FALSE;
}

bool Element::onSrcEvent(SrcPort& port, GstEvent* event) {
    return gst_pad_event_default(port.pad(), GST_OBJECT_CAST(m_raw), event) != FALSE;
}

bool Element::onSrcQuery(SrcPort& /*port*/, GstQuery* /*query*/) {
    return false;
}

bool Element::proposeAllocation(SinkPort& /*port*/, GstQuery* /*query*/) {
    return false;
}

bool Element::decideAllocation(SrcPort& port, GstQuery* query) {
    if (!query) {
        return false;
    }
    BufferPool chosenPool;
    guint size = 0;
    guint minBuffers = 2;
    guint maxBuffers = 0;

    if (gst_query_get_n_allocation_pools(query) > 0) {
        GstBufferPool* rawPool = nullptr;
        gst_query_parse_nth_allocation_pool(query, 0, &rawPool, &size, &minBuffers, &maxBuffers);
        if (rawPool) {
            chosenPool = BufferPool::adopt(rawPool);
        }
    }

    Caps caps = port.currentCaps().value_or(Caps());
    if (!chosenPool.isValid()) {
        chosenPool = BufferPool::createSystemPool();
    }

    if (chosenPool.isValid() && !chosenPool.isActive()) {
        chosenPool.configure(caps, size, minBuffers > 0 ? minBuffers : 2, maxBuffers);
    }
    port.setPool(std::move(chosenPool));
    return true;
}

bool Element::onGap(SinkPort& /*port*/, GstClockTime /*pts*/, GstClockTime /*duration*/) {
    return false;
}

bool Element::onSeek(
    SrcPort& /*port*/,
    double /*rate*/,
    GstFormat /*format*/,
    GstSeekFlags /*flags*/,
    GstSeekType /*startType*/,
    gint64 /*start*/,
    GstSeekType /*stopType*/,
    gint64 /*stop*/) {
    return false;
}

bool Element::queryDuration(SrcPort& /*port*/, GstFormat /*format*/, gint64& /*durationOut*/) {
    return false;
}

bool Element::queryPosition(SrcPort& /*port*/, GstFormat /*format*/, gint64& /*positionOut*/) {
    return false;
}

bool Element::querySeeking(
    SrcPort& /*port*/,
    GstFormat /*format*/,
    gboolean& /*seekableOut*/,
    gint64& /*segmentStartOut*/,
    gint64& /*segmentEndOut*/) {
    return false;
}

bool Element::setUri(const std::string& /*uri*/, std::string& errorOut) {
    errorOut = "URI handler not implemented";
    return false;
}

void Element::setClockProvider(bool enabled) noexcept {
    m_clockProvider = enabled;
    if (m_raw) {
        GST_OBJECT_LOCK(m_raw);
        if (enabled) {
            GST_OBJECT_FLAG_SET(m_raw, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
        } else {
            GST_OBJECT_FLAG_UNSET(m_raw, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
        }
        GST_OBJECT_UNLOCK(m_raw);
    }
}

SinkPort* Element::onRequestSinkPort(const std::string& /*name*/, const Caps* /*caps*/) {
    return nullptr;
}

SrcPort* Element::onRequestSrcPort(const std::string& /*name*/, const Caps* /*caps*/) {
    return nullptr;
}

void Element::onReleasePort(const std::string& /*name*/) {}

SinkPort* Element::sinkPort(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    auto it = m_sinkPorts.find(name);
    return it != m_sinkPorts.end() ? it->second.get() : nullptr;
}

SrcPort* Element::srcPort(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    auto it = m_srcPorts.find(name);
    return it != m_srcPorts.end() ? it->second.get() : nullptr;
}

std::vector<SinkPort*> Element::allSinkPorts() const {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    std::vector<SinkPort*> out;
    out.reserve(m_sinkPorts.size());
    for (const auto& [name, ptr] : m_sinkPorts) {
        out.push_back(ptr.get());
    }
    return out;
}

std::vector<SrcPort*> Element::allSrcPorts() const {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    std::vector<SrcPort*> out;
    out.reserve(m_srcPorts.size());
    for (const auto& [name, ptr] : m_srcPorts) {
        out.push_back(ptr.get());
    }
    return out;
}

SrcPort& Element::addSometimesSrcPort(const std::string& name, const Caps& templateCaps, const std::string& templateName) {
    GstElementClass* klass = GST_ELEMENT_GET_CLASS(m_raw);
    GstPadTemplate* tmpl = nullptr;
    if (!templateName.empty()) {
        tmpl = gst_element_class_get_pad_template(klass, templateName.c_str());
    }
    if (!tmpl) {
        tmpl = gst_element_class_get_pad_template(klass, name.c_str());
    }
    PortSpec spec;
    spec.name = name;
    spec.direction = GST_PAD_SRC;
    spec.presence = Presence::Sometimes;
    spec.templateCaps = templateCaps;

    SrcPort& port = createSrcPort(spec, tmpl, name);
    gst_pad_set_active(port.pad(), TRUE);
    gst_element_add_pad(m_raw, port.pad());
    return port;
}

bool Element::removeSometimesPort(const std::string& name) {
    std::unique_ptr<SrcPort> removedPort;
    GstPad* pad = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_portsMutex);
        auto it = m_srcPorts.find(name);
        if (it == m_srcPorts.end()) {
            return false;
        }
        removedPort = std::move(it->second);
        m_srcPorts.erase(it);
        pad = removedPort->pad();
        if (pad) {
            m_internalLinks.erase(pad);
            for (auto& [k, vec] : m_internalLinks) {
                vec.erase(std::remove(vec.begin(), vec.end(), pad), vec.end());
            }
            m_linkCookie.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    if (m_flowCombiner && pad) {
        std::lock_guard<std::mutex> fcLock(m_flowCombinerMutex);
        gst_flow_combiner_remove_pad(m_flowCombiner, pad);
    }
    if (pad) {
        GST_OBJECT_LOCK(m_raw);
        ++m_iterCookie;
        GST_OBJECT_UNLOCK(m_raw);
        gst_pad_set_active(pad, FALSE);
        gst_pad_set_element_private(pad, nullptr);
        gst_element_remove_pad(m_raw, pad);
    }
    return true;
}

void Element::noMorePads() {
    gst_element_no_more_pads(m_raw);
}

void Element::linkInternal(Port& a, Port& b) {
    {
        std::lock_guard<std::mutex> lock(m_portsMutex);
        m_internalLinks[a.pad()].push_back(b.pad());
        m_internalLinks[b.pad()].push_back(a.pad());
        m_linkCookie.fetch_add(1, std::memory_order_acq_rel);
    }
    GST_OBJECT_LOCK(m_raw);
    ++m_iterCookie;
    GST_OBJECT_UNLOCK(m_raw);
}

namespace {

struct PadVecIter {
    GstIterator parent;
    std::vector<GstPad*>* pads;
    size_t index;
};

void padVecIterCopy(const GstIterator* it, GstIterator* copy) {
    const auto* src = reinterpret_cast<const PadVecIter*>(it);
    auto* dst = reinterpret_cast<PadVecIter*>(copy);
    dst->pads = new std::vector<GstPad*>(*src->pads);
    for (GstPad* p : *dst->pads) {
        gst_object_ref(p);
    }
    dst->index = src->index;
}

GstIteratorResult padVecIterNext(GstIterator* it, GValue* result) {
    auto* iter = reinterpret_cast<PadVecIter*>(it);
    if (iter->index >= iter->pads->size()) {
        return GST_ITERATOR_DONE;
    }
    g_value_set_object(result, (*iter->pads)[iter->index++]);
    return GST_ITERATOR_OK;
}

void padVecIterResync(GstIterator* it) {
    auto* iter = reinterpret_cast<PadVecIter*>(it);
    iter->index = 0;
}

void padVecIterFree(GstIterator* it) {
    auto* iter = reinterpret_cast<PadVecIter*>(it);
    if (iter->pads) {
        for (GstPad* p : *iter->pads) {
            gst_object_unref(p);
        }
        delete iter->pads;
        iter->pads = nullptr;
    }
}

}  // namespace

GstIterator* Element::iterateInternalLinks(GstPad* pad) {
    std::vector<GstPad*> linked;
    {
        std::lock_guard<std::mutex> lock(m_portsMutex);
        auto it = m_internalLinks.find(pad);
        if (it != m_internalLinks.end()) {
            linked = it->second;
        }
    }
    if (linked.empty()) {
        return gst_pad_iterate_internal_links_default(pad, GST_OBJECT_CAST(m_raw));
    }

    auto* vec = new std::vector<GstPad*>(std::move(linked));
    for (GstPad* p : *vec) {
        gst_object_ref(p);
    }

    GstIterator* iter = gst_iterator_new(
        sizeof(PadVecIter),
        GST_TYPE_PAD,
        GST_OBJECT_GET_LOCK(m_raw),
        &m_iterCookie,
        &padVecIterCopy,
        &padVecIterNext,
        nullptr,
        &padVecIterResync,
        &padVecIterFree);
    auto* pIter = reinterpret_cast<PadVecIter*>(iter);
    pIter->pads = vec;
    pIter->index = 0;
    return iter;
}

Caps Element::querySinkCaps(SinkPort& port, const Caps& filter) {
    std::vector<GstPad*> linked;
    {
        std::lock_guard<std::mutex> lock(m_portsMutex);
        auto it = m_internalLinks.find(port.pad());
        if (it != m_internalLinks.end()) {
            linked = it->second;
            for (GstPad* p : linked) {
                gst_object_ref(p);
            }
        }
    }
    struct PadRefGuard {
        std::vector<GstPad*>& pads;
        ~PadRefGuard() {
            for (GstPad* p : pads) {
                gst_object_unref(p);
            }
        }
    } guard{linked};

    Caps result = port.templateCaps();
    if (!linked.empty()) {
        Caps combined = Caps::empty();
        bool anyPeer = false;
        for (GstPad* srcPad : linked) {
            GstCaps* peerRaw = gst_pad_peer_query_caps(srcPad, nullptr);
            if (peerRaw) {
                anyPeer = true;
                Caps transformed = transformCaps(GST_PAD_SRC, Caps::adopt(peerRaw), Caps());
                if (transformed.isValid() && !transformed.isEmpty()) {
                    GstCaps* merged = gst_caps_merge(combined.release(), transformed.release());
                    combined = Caps::adopt(merged);
                }
            }
        }
        if (anyPeer && combined.isValid() && !combined.isEmpty()) {
            result = result.intersect(combined);
        }
    }

    if (filter.isValid()) {
        result = result.intersect(filter);
    }
    return result;
}

bool Element::acceptSinkCaps(SinkPort& port, const Caps& caps) {
    if (!caps.isValid()) {
        return false;
    }
    return port.templateCaps().canIntersect(caps);
}

Caps Element::querySrcCaps(SrcPort& port, const Caps& filter) {
    std::vector<GstPad*> linked;
    {
        std::lock_guard<std::mutex> lock(m_portsMutex);
        auto it = m_internalLinks.find(port.pad());
        if (it != m_internalLinks.end()) {
            linked = it->second;
            for (GstPad* p : linked) {
                gst_object_ref(p);
            }
        }
    }
    struct PadRefGuard {
        std::vector<GstPad*>& pads;
        ~PadRefGuard() {
            for (GstPad* p : pads) {
                gst_object_unref(p);
            }
        }
    } guard{linked};

    Caps result = port.templateCaps();
    if (!linked.empty()) {
        Caps combined = Caps::empty();
        bool anyUpstream = false;
        for (GstPad* sinkPad : linked) {
            auto* sinkPortPtr = static_cast<SinkPort*>(gst_pad_get_element_private(sinkPad));
            Caps upstreamCaps;
            if (sinkPortPtr) {
                if (auto cur = sinkPortPtr->currentCaps()) {
                    upstreamCaps = *cur;
                }
            }
            if (!upstreamCaps.isValid()) {
                GstCaps* peerRaw = gst_pad_peer_query_caps(sinkPad, nullptr);
                if (peerRaw) {
                    upstreamCaps = Caps::adopt(peerRaw);
                }
            }
            if (upstreamCaps.isValid()) {
                anyUpstream = true;
                Caps transformed = transformCaps(GST_PAD_SINK, upstreamCaps, Caps());
                if (transformed.isValid() && !transformed.isEmpty()) {
                    GstCaps* merged = gst_caps_merge(combined.release(), transformed.release());
                    combined = Caps::adopt(merged);
                }
            }
        }
        if (anyUpstream && combined.isValid() && !combined.isEmpty()) {
            result = result.intersect(combined);
        }
    }

    if (filter.isValid()) {
        result = result.intersect(filter);
    }
    return result;
}

void Element::syncProperties(GstClockTime timestamp) {
    if (GST_CLOCK_TIME_IS_VALID(timestamp)) {
        gst_object_sync_values(GST_OBJECT_CAST(m_raw), timestamp);
    }
}

void Element::setSinkRole(bool isSink) {
    m_sinkRole = isSink;
    if (isSink) {
        GST_OBJECT_FLAG_SET(m_raw, GST_ELEMENT_FLAG_SINK);
    } else {
        GST_OBJECT_FLAG_UNSET(m_raw, GST_ELEMENT_FLAG_SINK);
    }
}

void Element::commitPreroll() {
    if (!m_sinkRole) {
        return;
    }
    bool postAsyncDone = false;
    {
        std::lock_guard<std::mutex> lock(m_prerollMutex);
        if (!m_prerolled.exchange(true, std::memory_order_acq_rel)) {
            if (!m_inReadyToPaused && m_asyncPosted) {
                m_asyncPosted = false;
                postAsyncDone = true;
            }
        }
    }
    if (postAsyncDone) {
        GST_OBJECT_LOCK(m_raw);
        if (GST_STATE(m_raw) == GST_STATE_READY && GST_STATE_NEXT(m_raw) == GST_STATE_PAUSED) {
            GST_STATE(m_raw) = GST_STATE_PAUSED;
            GST_STATE_NEXT(m_raw) = GST_STATE_VOID_PENDING;
            if (GST_STATE_PENDING(m_raw) == GST_STATE_PAUSED) {
                GST_STATE_PENDING(m_raw) = GST_STATE_VOID_PENDING;
            }
            GST_STATE_RETURN(m_raw) = GST_STATE_CHANGE_SUCCESS;
            GST_STATE_BROADCAST(m_raw);
        }
        GST_OBJECT_UNLOCK(m_raw);
        gst_element_post_message(m_raw, gst_message_new_async_done(GST_OBJECT_CAST(m_raw), GST_CLOCK_TIME_NONE));
    }
}

void Element::postOrDeferEos() {
    commitPreroll();
    bool isPlaying = false;
    GST_OBJECT_LOCK(m_raw);
    isPlaying = (GST_STATE(m_raw) == GST_STATE_PLAYING);
    if (!isPlaying) {
        m_eosDeferred.store(true, std::memory_order_release);
    }
    GST_OBJECT_UNLOCK(m_raw);

    if (isPlaying) {
        gst_element_post_message(m_raw, gst_message_new_eos(GST_OBJECT_CAST(m_raw)));
    }
}

GstFlowReturn Element::updateFlowCombiner(GstPad* pad, GstFlowReturn ret) {
    if (!m_flowCombiner) {
        return ret;
    }
    std::lock_guard<std::mutex> fcLock(m_flowCombinerMutex);
    return gst_flow_combiner_update_pad_flow(m_flowCombiner, pad, ret);
}

void Element::reportException(const char* where, const char* what) {
    GST_ELEMENT_ERROR(m_raw, LIBRARY, FAILED, ("%s: %s", where, what), (nullptr));
}

SinkPort& Element::createSinkPort(const PortSpec& spec, GstPadTemplate* tmpl, const std::string& padName) {
    GstPad* pad = tmpl ? gst_pad_new_from_template(tmpl, padName.c_str())
                       : gst_pad_new(padName.c_str(), GST_PAD_SINK);
    PortSpec copySpec = spec;
    copySpec.name = padName;
    auto port = std::make_unique<SinkPort>(this, pad, std::move(copySpec));
    SinkPort* rawPtr = port.get();
    std::lock_guard<std::mutex> lock(m_portsMutex);
    m_sinkPorts[padName] = std::move(port);
    return *rawPtr;
}

SrcPort& Element::createSrcPort(const PortSpec& spec, GstPadTemplate* tmpl, const std::string& padName) {
    GstPad* pad = tmpl ? gst_pad_new_from_template(tmpl, padName.c_str())
                       : gst_pad_new(padName.c_str(), GST_PAD_SRC);
    PortSpec copySpec = spec;
    copySpec.name = padName;
    auto port = std::make_unique<SrcPort>(this, pad, std::move(copySpec));
    SrcPort* rawPtr = port.get();
    if (m_flowCombiner) {
        std::lock_guard<std::mutex> fcLock(m_flowCombinerMutex);
        gst_flow_combiner_add_pad(m_flowCombiner, pad);
    }
    std::lock_guard<std::mutex> lock(m_portsMutex);
    m_srcPorts[padName] = std::move(port);
    return *rawPtr;
}

void Element::initAlwaysPorts(const std::vector<PortSpec>& specs) {
    GstElementClass* klass = GST_ELEMENT_GET_CLASS(m_raw);
    for (const auto& spec : specs) {
        if (spec.presence != Presence::Always) {
            continue;
        }
        GstPadTemplate* tmpl = gst_element_class_get_pad_template(klass, spec.name.c_str());
        if (spec.direction == GST_PAD_SINK) {
            SinkPort& p = createSinkPort(spec, tmpl, spec.name);
            gst_element_add_pad(m_raw, p.pad());
        } else if (spec.direction == GST_PAD_SRC) {
            SrcPort& p = createSrcPort(spec, tmpl, spec.name);
            gst_element_add_pad(m_raw, p.pad());
        }
    }
}

GstStateChangeReturn Element::handleChangeState(GstStateChange transition, GstElementClass* parentClass) {
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            {
                std::lock_guard<std::mutex> lock(m_prerollMutex);
                m_prerolled.store(false, std::memory_order_release);
                m_inReadyToPaused = false;
                m_asyncPosted = false;
            }
            m_eosDeferred.store(false, std::memory_order_release);
            if (!open()) {
                return GST_STATE_CHANGE_FAILURE;
            }
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            for (auto* s : allSinkPorts()) {
                s->resetUnblock();
            }
            {
                std::lock_guard<std::mutex> lock(m_prerollMutex);
                m_prerolled.store(false, std::memory_order_release);
                m_inReadyToPaused = true;
                m_asyncPosted = false;
            }
            if (!start()) {
                {
                    std::lock_guard<std::mutex> lock(m_prerollMutex);
                    m_inReadyToPaused = false;
                    m_asyncPosted = false;
                }
                unblock();
                for (auto* s : allSinkPorts()) {
                    s->unblock();
                }
                stop();
                return GST_STATE_CHANGE_FAILURE;
            }
            break;
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            if (!pause()) {
                return GST_STATE_CHANGE_FAILURE;
            }
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            unblock();
            for (auto* s : allSinkPorts()) {
                s->unblock();
            }
            break;
        default:
            break;
    }

    GstStateChangeReturn ret = parentClass->change_state(m_raw, transition);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
            {
                std::lock_guard<std::mutex> lock(m_prerollMutex);
                m_inReadyToPaused = false;
                m_asyncPosted = false;
            }
            unblock();
            for (auto* s : allSinkPorts()) {
                s->unblock();
            }
            stop();
        }
        return ret;
    }

    switch (transition) {
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            if (m_isLive) {
                std::lock_guard<std::mutex> lock(m_prerollMutex);
                m_inReadyToPaused = false;
                m_asyncPosted = false;
                ret = GST_STATE_CHANGE_NO_PREROLL;
            } else if (m_sinkRole) {
                std::lock_guard<std::mutex> lock(m_prerollMutex);
                m_inReadyToPaused = false;
                if (m_prerolled.load(std::memory_order_acquire)) {
                    ret = GST_STATE_CHANGE_SUCCESS;
                } else {
                    m_asyncPosted = true;
                    gst_element_post_message(m_raw, gst_message_new_async_start(GST_OBJECT_CAST(m_raw)));
                    ret = GST_STATE_CHANGE_ASYNC;
                }
            } else {
                std::lock_guard<std::mutex> lock(m_prerollMutex);
                m_inReadyToPaused = false;
            }
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            if (!play()) {
                pause();
                parentClass->change_state(m_raw, GST_STATE_CHANGE_PLAYING_TO_PAUSED);
                return GST_STATE_CHANGE_FAILURE;
            }
            if (m_sinkRole && m_eosDeferred.exchange(false, std::memory_order_acq_rel)) {
                gst_element_call_async(
                    m_raw,
                    [](GstElement* elem, gpointer /*data*/) {
                        gst_element_post_message(elem, gst_message_new_eos(GST_OBJECT_CAST(elem)));
                    },
                    nullptr,
                    nullptr);
            }
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            {
                std::lock_guard<std::mutex> lock(m_prerollMutex);
                m_prerolled.store(false, std::memory_order_release);
                m_inReadyToPaused = false;
                m_asyncPosted = false;
            }
            m_eosDeferred.store(false, std::memory_order_release);
            if (!stop()) {
                return GST_STATE_CHANGE_FAILURE;
            }
            for (auto* s : allSinkPorts()) {
                s->resetState();
            }
            for (auto* src : allSrcPorts()) {
                src->resetStickyState(false);
            }
            if (m_flowCombiner) {
                std::lock_guard<std::mutex> fcLock(m_flowCombinerMutex);
                gst_flow_combiner_reset(m_flowCombiner);
            }
            break;
        case GST_STATE_CHANGE_READY_TO_NULL:
            close();
            break;
        default:
            break;
    }

    return ret;
}

// ============================================================================
// GObject / GstElement Class & Trampolines
// ============================================================================

namespace {

GQuark uriMetaQuark() {
    static const GQuark q = g_quark_from_static_string("gps_elem_uri_handler_meta");
    return q;
}

GstURIType trampolineUriGetType(GType type) {
    auto* meta = static_cast<UriHandlerMeta*>(g_type_get_qdata(type, uriMetaQuark()));
    return meta ? meta->uriType : GST_URI_UNKNOWN;
}

const gchar* const* trampolineUriGetProtocols(GType type) {
    auto* meta = static_cast<UriHandlerMeta*>(g_type_get_qdata(type, uriMetaQuark()));
    if (!meta || meta->protocolPtrs.empty()) {
        static const gchar* empty[] = {nullptr};
        return empty;
    }
    return meta->protocolPtrs.data();
}

gchar* trampolineUriGetUri(GstURIHandler* handler) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(handler);
    if (!inst || !inst->impl) {
        return nullptr;
    }
    try {
        std::string uri = inst->impl->currentUri();
        return uri.empty() ? nullptr : g_strdup(uri.c_str());
    } catch (...) {
        return nullptr;
    }
}

gboolean trampolineUriSetUri(GstURIHandler* handler, const gchar* uri, GError** error) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(handler);
    if (!inst || !inst->impl) {
        return FALSE;
    }
    try {
        std::string err;
        if (inst->impl->setUri(uri ? std::string(uri) : std::string(), err)) {
            return TRUE;
        }
        if (error) {
            g_set_error_literal(
                error,
                GST_URI_ERROR,
                GST_URI_ERROR_BAD_URI,
                err.empty() ? "Failed to set URI" : err.c_str());
        }
        return FALSE;
    } catch (const std::exception& e) {
        if (error) {
            g_set_error_literal(error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, e.what());
        }
        return FALSE;
    } catch (...) {
        if (error) {
            g_set_error_literal(error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, "Unknown C++ exception");
        }
        return FALSE;
    }
}

void uriHandlerInterfaceInit(gpointer gIface, gpointer /*ifaceData*/) {
    auto* iface = static_cast<GstURIHandlerInterface*>(gIface);
    iface->get_type = &trampolineUriGetType;
    iface->get_protocols = &trampolineUriGetProtocols;
    iface->get_uri = &trampolineUriGetUri;
    iface->set_uri = &trampolineUriSetUri;
}

}  // namespace

GType Element::registerType(
    const std::string& typeName,
    ElementClassBuilder builder,
    std::function<std::unique_ptr<Element>(GstElement*)> factory) {
    if (GType existing = g_type_from_name(typeName.c_str())) {
        return existing;
    }

    auto* meta = new ElementClassMeta();
    meta->typeName = typeName;
    meta->builder = std::move(builder);
    meta->factory = std::move(factory);

    GTypeInfo info{};
    info.class_size = sizeof(GpsElementClass);
    info.class_init = &Element::classInit;
    info.class_data = meta;
    info.instance_size = sizeof(GpsElementInstance);
    info.instance_init = &Element::instanceInit;

    GType type = g_type_register_static(GST_TYPE_ELEMENT, typeName.c_str(), &info, static_cast<GTypeFlags>(0));

    if (meta->builder.uriMeta) {
        g_type_set_qdata(type, uriMetaQuark(), meta->builder.uriMeta.get());
        GInterfaceInfo uriInfo{};
        uriInfo.interface_init = &uriHandlerInterfaceInit;
        uriInfo.interface_finalize = nullptr;
        uriInfo.interface_data = meta->builder.uriMeta.get();
        g_type_add_interface_static(type, GST_TYPE_URI_HANDLER, &uriInfo);
    }

    for (const auto& ifaceSpec : meta->builder.interfaces) {
        if (ifaceSpec.interfaceType != G_TYPE_INVALID && ifaceSpec.interfaceInit) {
            GInterfaceInfo ifaceInfo{};
            ifaceInfo.interface_init = ifaceSpec.interfaceInit;
            ifaceInfo.interface_finalize = nullptr;
            ifaceInfo.interface_data = ifaceSpec.interfaceData;
            g_type_add_interface_static(type, ifaceSpec.interfaceType, &ifaceInfo);
        }
    }

    return type;
}

void Element::classInit(gpointer gClass, gpointer classData) {
    auto* klass = static_cast<GpsElementClass*>(gClass);
    auto* meta = static_cast<ElementClassMeta*>(classData);
    klass->meta = meta;
    meta->parentClass = g_type_class_peek_parent(gClass);

    GObjectClass* gobjectClass = G_OBJECT_CLASS(gClass);
    gobjectClass->set_property = &Element::trampolineSetProperty;
    gobjectClass->get_property = &Element::trampolineGetProperty;
    gobjectClass->finalize = &Element::trampolineFinalize;

    GstElementClass* elementClass = GST_ELEMENT_CLASS(gClass);
    elementClass->change_state = &Element::trampolineChangeState;
    elementClass->request_new_pad = &Element::trampolineRequestNewPad;
    elementClass->release_pad = &Element::trampolineReleasePad;
    elementClass->provide_clock = &Element::trampolineProvideClock;

    gst_element_class_set_static_metadata(
        elementClass,
        meta->builder.longName.c_str(),
        meta->builder.klass.c_str(),
        meta->builder.description.c_str(),
        meta->builder.author.c_str());

    for (const auto& spec : meta->builder.ports) {
        GstPadTemplate* tmpl = gst_pad_template_new(
            spec.name.c_str(),
            spec.direction,
            toGstPresence(spec.presence),
            spec.templateCaps.get());
        gst_element_class_add_pad_template(elementClass, tmpl);
    }

    meta->builder.properties.installAll(gobjectClass);
}

void Element::instanceInit(GTypeInstance* instance, gpointer gClass) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(instance);
    auto* klass = reinterpret_cast<GpsElementClass*>(gClass);
    inst->impl = nullptr;
    if (klass && klass->meta && klass->meta->factory) {
        try {
            auto impl = klass->meta->factory(&inst->parent);
            inst->impl = impl.release();
            inst->impl->initAlwaysPorts(klass->meta->builder.ports);
        } catch (const std::exception& e) {
            g_critical("Element instanceInit failed: %s", e.what());
        } catch (...) {
            g_critical("Element instanceInit failed with unknown exception");
        }
    }
}

void Element::trampolineSetProperty(GObject* object, guint propId, const GValue* value, GParamSpec* pspec) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(object);
    auto* klass = reinterpret_cast<GpsElementClass*>(G_OBJECT_GET_CLASS(object));
    if (!inst->impl || !klass || !klass->meta) {
        return;
    }
    try {
        if (!klass->meta->builder.properties.setProperty(*inst->impl, GST_ELEMENT_CAST(object), propId, value)) {
            if (propId > klass->meta->builder.properties.entries().size()) {
                G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
            }
        }
    } catch (const std::exception& e) {
        inst->impl->reportException("set_property", e.what());
    } catch (...) {
        inst->impl->reportException("set_property", "unknown C++ exception");
    }
}

void Element::trampolineGetProperty(GObject* object, guint propId, GValue* value, GParamSpec* pspec) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(object);
    auto* klass = reinterpret_cast<GpsElementClass*>(G_OBJECT_GET_CLASS(object));
    if (!inst->impl || !klass || !klass->meta) {
        return;
    }
    try {
        if (!klass->meta->builder.properties.getProperty(*inst->impl, GST_ELEMENT_CAST(object), propId, value)) {
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
        }
    } catch (const std::exception& e) {
        inst->impl->reportException("get_property", e.what());
    } catch (...) {
        inst->impl->reportException("get_property", "unknown C++ exception");
    }
}

void Element::trampolineFinalize(GObject* object) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(object);
    auto* klass = reinterpret_cast<GpsElementClass*>(G_OBJECT_GET_CLASS(object));
    delete inst->impl;
    inst->impl = nullptr;
    if (klass && klass->meta && klass->meta->parentClass) {
        G_OBJECT_CLASS(klass->meta->parentClass)->finalize(object);
    }
}

GstStateChangeReturn Element::trampolineChangeState(GstElement* element, GstStateChange transition) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(element);
    auto* klass = reinterpret_cast<GpsElementClass*>(G_OBJECT_GET_CLASS(element));
    auto* parentClass = klass && klass->meta ? GST_ELEMENT_CLASS(klass->meta->parentClass) : nullptr;
    if (!inst->impl || !parentClass) {
        return GST_STATE_CHANGE_FAILURE;
    }
    try {
        return inst->impl->handleChangeState(transition, parentClass);
    } catch (const std::exception& e) {
        inst->impl->reportException("change_state", e.what());
        return GST_STATE_CHANGE_FAILURE;
    } catch (...) {
        inst->impl->reportException("change_state", "unknown C++ exception");
        return GST_STATE_CHANGE_FAILURE;
    }
}

std::string Element::generateRequestPadName(const gchar* name, GstPadTemplate* templ) {
    if (name != nullptr) {
        return std::string(name);
    }
    const std::string tmplName = GST_PAD_TEMPLATE_NAME_TEMPLATE(templ);
    while (true) {
        const uint64_t idx = m_requestPadCounter.fetch_add(1, std::memory_order_relaxed);
        const std::string idxStr = std::to_string(idx);
        std::string candidate = tmplName;
        size_t pos = candidate.find("%u");
        if (pos == std::string::npos) {
            pos = candidate.find("%d");
        }
        if (pos != std::string::npos) {
            candidate.replace(pos, 2, idxStr);
        } else {
            candidate += "_" + idxStr;
        }
        std::lock_guard<std::mutex> lock(m_portsMutex);
        if (m_sinkPorts.find(candidate) == m_sinkPorts.end() &&
            m_srcPorts.find(candidate) == m_srcPorts.end()) {
            return candidate;
        }
    }
}

GstPad* Element::trampolineRequestNewPad(GstElement* element, GstPadTemplate* templ, const gchar* name, const GstCaps* caps) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(element);
    if (!inst->impl || !templ) {
        return nullptr;
    }
    try {
        std::string padName = inst->impl->generateRequestPadName(name, templ);
        Caps capsCopy = caps ? Caps::borrow(const_cast<GstCaps*>(caps)) : Caps();
        const Caps* capsPtr = caps ? &capsCopy : nullptr;

        if (templ->direction == GST_PAD_SINK) {
            SinkPort* port = inst->impl->onRequestSinkPort(padName, capsPtr);
            if (!port) {
                PortSpec spec;
                spec.name = padName;
                spec.direction = GST_PAD_SINK;
                spec.presence = Presence::Request;
                spec.templateCaps = Caps::borrow(GST_PAD_TEMPLATE_CAPS(templ));
                port = &inst->impl->createSinkPort(spec, templ, padName);
            }
            GstPad* pad = port->pad();
            gst_pad_set_active(pad, TRUE);
            if (!gst_element_add_pad(element, pad)) {
                gst_pad_set_active(pad, FALSE);
                std::lock_guard<std::mutex> lock(inst->impl->m_portsMutex);
                inst->impl->m_sinkPorts.erase(padName);
                return nullptr;
            }
            return pad;
        }
        if (templ->direction == GST_PAD_SRC) {
            SrcPort* port = inst->impl->onRequestSrcPort(padName, capsPtr);
            if (!port) {
                PortSpec spec;
                spec.name = padName;
                spec.direction = GST_PAD_SRC;
                spec.presence = Presence::Request;
                spec.templateCaps = Caps::borrow(GST_PAD_TEMPLATE_CAPS(templ));
                port = &inst->impl->createSrcPort(spec, templ, padName);
            }
            GstPad* pad = port->pad();
            gst_pad_set_active(pad, TRUE);
            if (!gst_element_add_pad(element, pad)) {
                gst_pad_set_active(pad, FALSE);
                if (inst->impl->m_flowCombiner) {
                    std::lock_guard<std::mutex> fcLock(inst->impl->m_flowCombinerMutex);
                    gst_flow_combiner_remove_pad(inst->impl->m_flowCombiner, pad);
                }
                std::lock_guard<std::mutex> lock(inst->impl->m_portsMutex);
                inst->impl->m_srcPorts.erase(padName);
                return nullptr;
            }
            return pad;
        }
    } catch (const std::exception& e) {
        inst->impl->reportException("request_new_pad", e.what());
    } catch (...) {
        inst->impl->reportException("request_new_pad", "unknown C++ exception");
    }
    return nullptr;
}

void Element::trampolineReleasePad(GstElement* element, GstPad* pad) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(element);
    if (!inst->impl || !pad) {
        return;
    }
    try {
        gchar* nameStr = gst_pad_get_name(pad);
        std::string padName = nameStr ? std::string(nameStr) : std::string();
        g_free(nameStr);

        inst->impl->onReleasePort(padName);

        std::unique_ptr<SinkPort> removedSink;
        std::unique_ptr<SrcPort> removedSrc;
        if (GST_PAD_DIRECTION(pad) == GST_PAD_SRC && inst->impl->m_flowCombiner) {
            std::lock_guard<std::mutex> fcLock(inst->impl->m_flowCombinerMutex);
            gst_flow_combiner_remove_pad(inst->impl->m_flowCombiner, pad);
        }
        {
            std::lock_guard<std::mutex> lock(inst->impl->m_portsMutex);
            inst->impl->m_internalLinks.erase(pad);
            for (auto& [k, vec] : inst->impl->m_internalLinks) {
                vec.erase(std::remove(vec.begin(), vec.end(), pad), vec.end());
            }
            inst->impl->m_linkCookie.fetch_add(1, std::memory_order_acq_rel);

            auto sit = inst->impl->m_sinkPorts.find(padName);
            if (sit != inst->impl->m_sinkPorts.end()) {
                removedSink = std::move(sit->second);
                inst->impl->m_sinkPorts.erase(sit);
            }
            auto rit = inst->impl->m_srcPorts.find(padName);
            if (rit != inst->impl->m_srcPorts.end()) {
                removedSrc = std::move(rit->second);
                inst->impl->m_srcPorts.erase(rit);
            }
        }

        GST_OBJECT_LOCK(element);
        ++inst->impl->m_iterCookie;
        GST_OBJECT_UNLOCK(element);

        gst_pad_set_active(pad, FALSE);
        gst_pad_set_element_private(pad, nullptr);
        gst_element_remove_pad(element, pad);
    } catch (const std::exception& e) {
        inst->impl->reportException("release_pad", e.what());
    } catch (...) {
        inst->impl->reportException("release_pad", "unknown C++ exception");
    }
}

GstClock* Element::trampolineProvideClock(GstElement* element) {
    auto* inst = reinterpret_cast<GpsElementInstance*>(element);
    if (!inst->impl || !inst->impl->isClockProvider()) {
        return nullptr;
    }
    try {
        return inst->impl->provideClock();
    } catch (const std::exception& e) {
        inst->impl->reportException("provide_clock", e.what());
        return nullptr;
    } catch (...) {
        inst->impl->reportException("provide_clock", "unknown C++ exception");
        return nullptr;
    }
}

}  // namespace gps::elem
