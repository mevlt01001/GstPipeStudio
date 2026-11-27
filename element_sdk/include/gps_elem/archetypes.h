#pragma once

#include "gps_elem/audio.h"
#include "gps_elem/clock.h"
#include "gps_elem/driver.h"
#include "gps_elem/element.h"
#include "gps_elem/video.h"

#include <condition_variable>
#include <mutex>
#include <optional>

namespace gps::elem {

class SourceElement : public Element {
public:
    explicit SourceElement(GstElement* raw) : Element(raw) {}

    static void addDefaultPorts(ElementClassBuilder& b, Caps srcCaps = Caps::any()) {
        b.addSrcPort("src", std::move(srcCaps), Presence::Always);
    }

    TaskDriver& driver() noexcept { return m_driver; }
    const TaskDriver& driver() const noexcept { return m_driver; }

    void setLiveSource(bool live) noexcept {
        setLive(live);
        m_driver.setLive(live);
    }

    bool start() override {
        SrcPort* src = srcPort("src");
        if (!src) {
            return false;
        }
        gst_pad_set_active(src->pad(), TRUE);
        return m_driver.start(this, [this, src]() {
            if (m_driver.isUnblocking()) {
                m_driver.pause();
                return;
            }
            GstFlowReturn ret = produce(*src);
            if (m_driver.isUnblocking()) {
                return;
            }
            if (ret == GST_FLOW_EOS) {
                src->pushEos();
                m_driver.pause();
            } else if (ret != GST_FLOW_OK) {
                m_driver.pause();
            }
        });
    }

    void unblock() override { m_driver.unblock(); }
    bool stop() override {
        m_driver.stop();
        return true;
    }

    virtual GstFlowReturn produce(SrcPort& src) = 0;

private:
    TaskDriver m_driver;
};

class SinkElement : public Element {
public:
    explicit SinkElement(GstElement* raw) : Element(raw) {
        setSinkRole(true);
    }

    static void addDefaultPorts(ElementClassBuilder& b, Caps sinkCaps = Caps::any()) {
        b.addSinkPort("sink", std::move(sinkCaps), Presence::Always, InputMode::Direct);
    }

    ClockSync& clockSync() noexcept { return m_clockSync; }

    bool open() override {
        SinkPort* sink = sinkPort("sink");
        if (sink) {
            sink->setDirectCallback([this, sink](Buffer buf) {
                commitPreroll();
                {
                    std::unique_lock<std::mutex> lock(m_prerollWaitMutex);
                    m_prerollWaitCv.wait(lock, [this, sink]() {
                        return m_inPlaying || m_prerollUnblocked ||
                               sink->isFlushing() || sink->isUnblocked();
                    });
                    if (!m_inPlaying || m_prerollUnblocked ||
                        sink->isFlushing() || sink->isUnblocked()) {
                        return GST_FLOW_FLUSHING;
                    }
                }
                if (m_clockSync.isEnabled()) {
                    GstClockReturn cret = m_clockSync.waitForPts(raw(), sink->segment(), buf.pts());
                    if (cret == GST_CLOCK_UNSCHEDULED) {
                        return GST_FLOW_FLUSHING;
                    }
                }
                return consume(buf);
            });
        }
        return true;
    }

    bool start() override {
        {
            std::lock_guard<std::mutex> lock(m_prerollWaitMutex);
            m_inPlaying = false;
            m_prerollUnblocked = false;
        }
        m_clockSync.resetUnblock();
        return true;
    }

    bool play() override {
        {
            std::lock_guard<std::mutex> lock(m_prerollWaitMutex);
            m_inPlaying = true;
        }
        m_prerollWaitCv.notify_all();
        return true;
    }

    bool pause() override {
        {
            std::lock_guard<std::mutex> lock(m_prerollWaitMutex);
            m_inPlaying = false;
        }
        m_prerollWaitCv.notify_all();
        return true;
    }

    void unblock() override {
        {
            std::lock_guard<std::mutex> lock(m_prerollWaitMutex);
            m_prerollUnblocked = true;
        }
        m_prerollWaitCv.notify_all();
        m_clockSync.unblock();
    }

    bool onSinkEvent(SinkPort& port, GstEvent* event) override {
        switch (GST_EVENT_TYPE(event)) {
            case GST_EVENT_EOS:
                gst_event_unref(event);
                postOrDeferEos();
                return true;
            case GST_EVENT_GAP:
                commitPreroll();
                break;
            case GST_EVENT_FLUSH_START:
                {
                    std::lock_guard<std::mutex> lock(m_prerollWaitMutex);
                    m_prerollUnblocked = true;
                }
                m_prerollWaitCv.notify_all();
                m_clockSync.unblock();
                break;
            case GST_EVENT_FLUSH_STOP:
                {
                    std::lock_guard<std::mutex> lock(m_prerollWaitMutex);
                    m_prerollUnblocked = false;
                }
                m_clockSync.resetUnblock();
                clearDeferredEos();
                break;
            default:
                break;
        }
        return Element::onSinkEvent(port, event);
    }

    virtual GstFlowReturn consume(Buffer& buf) = 0;

private:
    ClockSync m_clockSync;
    std::mutex m_prerollWaitMutex;
    std::condition_variable m_prerollWaitCv;
    bool m_inPlaying = false;
    bool m_prerollUnblocked = false;
};

class TransformElement : public Element {
public:
    explicit TransformElement(GstElement* raw) : Element(raw) {}

    static void addDefaultPorts(ElementClassBuilder& b, Caps sinkCaps = Caps::any(), Caps srcCaps = Caps::any()) {
        b.addSinkPort("sink", std::move(sinkCaps), Presence::Always, InputMode::Direct);
        b.addSrcPort("src", std::move(srcCaps), Presence::Always);
    }

    bool open() override {
        SinkPort* sink = sinkPort("sink");
        SrcPort* src = srcPort("src");
        if (sink && src) {
            linkInternal(*sink, *src);
            sink->setDirectCallback([this, src](Buffer buf) {
                return transform(buf, *src);
            });
        }
        return true;
    }

    bool onSinkEvent(SinkPort& port, GstEvent* event) override {
        switch (GST_EVENT_TYPE(event)) {
            case GST_EVENT_STREAM_START: {
                const gchar* streamId = nullptr;
                gst_event_parse_stream_start(event, &streamId);
                if (streamId) {
                    if (SrcPort* src = srcPort("src")) {
                        src->setStreamId(streamId);
                    }
                }
                gst_event_unref(event);
                return true;
            }
            case GST_EVENT_CAPS:
                gst_event_unref(event);
                return true;
            case GST_EVENT_SEGMENT: {
                GstSegment seg;
                gst_event_copy_segment(event, &seg);
                if (SrcPort* src = srcPort("src")) {
                    src->setSegment(seg);
                }
                gst_event_unref(event);
                return true;
            }
            default:
                break;
        }
        return Element::onSinkEvent(port, event);
    }

    virtual GstFlowReturn transform(Buffer& buf, SrcPort& src) = 0;
};

class VideoFilterElement : public TransformElement {
public:
    explicit VideoFilterElement(GstElement* raw) : TransformElement(raw) {}

    static void addDefaultPorts(
        ElementClassBuilder& b,
        const VideoRawSpec& sinkSpec = {},
        const VideoRawSpec& srcSpec = {}) {
        b.addSinkPort("sink", Caps::videoRaw(sinkSpec), Presence::Always, InputMode::Direct);
        b.addSrcPort("src", Caps::videoRaw(srcSpec), Presence::Always);
    }

    bool onSinkCaps(SinkPort& /*port*/, const Caps& caps) override {
        auto inInfo = VideoInfo::fromCaps(caps);
        if (!inInfo.has_value()) {
            return false;
        }
        m_inInfo = *inInfo;

        SrcPort* src = srcPort("src");
        if (!src) {
            return false;
        }

        // Negotiate output caps with downstream peer
        Caps candidate = transformCaps(GST_PAD_SINK, caps, Caps());
        if (GstCaps* peerRaw = gst_pad_peer_query_caps(src->pad(), candidate.get())) {
            Caps peerCaps = Caps::adopt(peerRaw);
            if (!peerCaps.isEmpty()) {
                candidate = peerCaps;
            }
        }
        candidate = candidate.intersect(src->templateCaps());
        if (candidate.isEmpty()) {
            return false;
        }
        if (!candidate.isFixed()) {
            candidate = candidate.fixate();
        }
        auto outInfo = VideoInfo::fromCaps(candidate);
        if (!outInfo.has_value()) {
            return false;
        }
        m_outInfo = *outInfo;
        src->setCaps(candidate);
        return onVideoConfigured(*m_inInfo, *m_outInfo);
    }

    virtual bool onVideoConfigured(const VideoInfo& /*inInfo*/, const VideoInfo& /*outInfo*/) {
        return true;
    }

    virtual bool isInPlace() const {
        return m_inInfo.has_value() && m_outInfo.has_value() &&
               m_inInfo->format() == m_outInfo->format() &&
               m_inInfo->width() == m_outInfo->width() &&
               m_inInfo->height() == m_outInfo->height();
    }

    GstFlowReturn transform(Buffer& buf, SrcPort& src) override {
        if (!m_inInfo.has_value() || !m_outInfo.has_value()) {
            return GST_FLOW_NOT_NEGOTIATED;
        }

        if (isInPlace()) {
            auto frame = VideoFrame::mapWrite(*m_inInfo, buf);
            if (!frame.has_value()) {
                return GST_FLOW_ERROR;
            }
            GstFlowReturn ret = filterInPlace(*frame);
            frame->unmap();
            if (ret != GST_FLOW_OK) {
                return ret;
            }
            return src.push(std::move(buf));
        }

        auto inFrame = VideoFrame::mapRead(*m_inInfo, buf);
        if (!inFrame.has_value()) {
            return GST_FLOW_ERROR;
        }
        Buffer outBuf = Buffer::allocate(m_outInfo->size());
        if (!outBuf.isValid()) {
            return GST_FLOW_ERROR;
        }
        outBuf.copyMetadataFrom(buf);
        auto outFrame = VideoFrame::mapWrite(*m_outInfo, outBuf);
        if (!outFrame.has_value()) {
            return GST_FLOW_ERROR;
        }
        GstFlowReturn ret = filterOutOfPlace(*inFrame, *outFrame);
        outFrame->unmap();
        inFrame->unmap();
        if (ret != GST_FLOW_OK) {
            return ret;
        }
        return src.push(std::move(outBuf));
    }

    virtual GstFlowReturn filterInPlace(VideoFrame& /*frame*/) { return GST_FLOW_OK; }
    virtual GstFlowReturn filterOutOfPlace(const VideoFrame& /*inFrame*/, VideoFrame& /*outFrame*/) { return GST_FLOW_OK; }

protected:
    std::optional<VideoInfo> m_inInfo;
    std::optional<VideoInfo> m_outInfo;
};

class AudioFilterElement : public TransformElement {
public:
    explicit AudioFilterElement(GstElement* raw) : TransformElement(raw) {}

    static void addDefaultPorts(
        ElementClassBuilder& b,
        const AudioRawSpec& sinkSpec = {},
        const AudioRawSpec& srcSpec = {}) {
        b.addSinkPort("sink", Caps::audioRaw(sinkSpec), Presence::Always, InputMode::Direct);
        b.addSrcPort("src", Caps::audioRaw(srcSpec), Presence::Always);
    }

    bool onSinkCaps(SinkPort& /*port*/, const Caps& caps) override {
        auto inInfo = AudioInfo::fromCaps(caps);
        if (!inInfo.has_value()) {
            return false;
        }
        m_inInfo = *inInfo;

        SrcPort* src = srcPort("src");
        if (!src) {
            return false;
        }

        Caps candidate = transformCaps(GST_PAD_SINK, caps, Caps());
        if (GstCaps* peerRaw = gst_pad_peer_query_caps(src->pad(), candidate.get())) {
            Caps peerCaps = Caps::adopt(peerRaw);
            if (!peerCaps.isEmpty()) {
                candidate = peerCaps;
            }
        }
        candidate = candidate.intersect(src->templateCaps());
        if (candidate.isEmpty()) {
            return false;
        }
        if (!candidate.isFixed()) {
            candidate = candidate.fixate();
        }
        auto outInfo = AudioInfo::fromCaps(candidate);
        if (!outInfo.has_value()) {
            return false;
        }
        m_outInfo = *outInfo;
        src->setCaps(candidate);
        return onAudioConfigured(*m_inInfo, *m_outInfo);
    }

    virtual bool onAudioConfigured(const AudioInfo& /*inInfo*/, const AudioInfo& /*outInfo*/) {
        return true;
    }

    virtual bool isInPlace() const {
        return m_inInfo.has_value() && m_outInfo.has_value() &&
               m_inInfo->format() == m_outInfo->format() &&
               m_inInfo->rate() == m_outInfo->rate() &&
               m_inInfo->channels() == m_outInfo->channels() &&
               m_inInfo->layout() == m_outInfo->layout();
    }

    GstFlowReturn transform(Buffer& buf, SrcPort& src) override {
        if (!m_inInfo.has_value() || !m_outInfo.has_value()) {
            return GST_FLOW_NOT_NEGOTIATED;
        }

        if (isInPlace()) {
            auto frame = AudioFrame::mapWrite(*m_inInfo, buf);
            if (!frame.has_value()) {
                return GST_FLOW_ERROR;
            }
            GstFlowReturn ret = filterInPlace(*frame);
            frame->unmap();
            if (ret != GST_FLOW_OK) {
                return ret;
            }
            return src.push(std::move(buf));
        }

        auto inFrame = AudioFrame::mapRead(*m_inInfo, buf);
        if (!inFrame.has_value()) {
            return GST_FLOW_ERROR;
        }
        const size_t outBytes = static_cast<size_t>(inFrame->samples()) * static_cast<size_t>(m_outInfo->bpf());
        Buffer outBuf = Buffer::allocate(outBytes);
        if (!outBuf.isValid()) {
            return GST_FLOW_ERROR;
        }
        outBuf.copyMetadataFrom(buf);
        auto outFrame = AudioFrame::mapWrite(*m_outInfo, outBuf);
        if (!outFrame.has_value()) {
            return GST_FLOW_ERROR;
        }
        GstFlowReturn ret = filterOutOfPlace(*inFrame, *outFrame);
        outFrame->unmap();
        inFrame->unmap();
        if (ret != GST_FLOW_OK) {
            return ret;
        }
        return src.push(std::move(outBuf));
    }

    virtual GstFlowReturn filterInPlace(AudioFrame& /*frame*/) { return GST_FLOW_OK; }
    virtual GstFlowReturn filterOutOfPlace(const AudioFrame& /*inFrame*/, AudioFrame& /*outFrame*/) { return GST_FLOW_OK; }

protected:
    std::optional<AudioInfo> m_inInfo;
    std::optional<AudioInfo> m_outInfo;
};

}  // namespace gps::elem
