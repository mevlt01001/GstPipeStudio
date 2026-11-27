#include "gps_elem/archetypes.h"
#include "gps_elem/barrier.h"
#include "gps_elem/meta.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

// 1. Request-pad element for nullptr name + release + internal-links UAF test
class AdvSplitter : public gps::elem::Element {
public:
    explicit AdvSplitter(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Adv Splitter", "Filter/Test", "Request pad adversarial test", "GstPipeStudio");
        b.addSinkPort("sink", gps::elem::Caps::any(), gps::elem::Presence::Always);
        b.addSrcPort("src_%u", gps::elem::Caps::any(), gps::elem::Presence::Request);
    }

    gps::elem::SrcPort* onRequestSrcPort(const std::string& name, const gps::elem::Caps* /*caps*/) override {
        GstElementClass* klass = GST_ELEMENT_GET_CLASS(raw());
        GstPadTemplate* tmpl = gst_element_class_get_pad_template(klass, "src_%u");
        gps::elem::PortSpec spec;
        spec.name = name;
        spec.direction = GST_PAD_SRC;
        spec.presence = gps::elem::Presence::Request;
        spec.templateCaps = gps::elem::Caps::any();
        GstPad* pad = tmpl ? gst_pad_new_from_template(tmpl, name.c_str())
                           : gst_pad_new(name.c_str(), GST_PAD_SRC);
        // Use Element's helper by returning nullptr? Wait: if we return nullptr, Element creates it,
        // so we can link in open/later OR we can just use default creation and link via sink->src.
        gst_object_unref(pad);
        return nullptr;
    }

    void linkAll() {
        auto* sink = sinkPort("sink");
        if (!sink) return;
        for (auto* src : allSrcPorts()) {
            linkInternal(*sink, *src);
        }
    }
};

// 2. SinkElement for preroll & FLUSH_STOP deferred EOS test
class AdvSink : public gps::elem::SinkElement {
public:
    explicit AdvSink(GstElement* raw) : SinkElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Adv Sink", "Sink/Test", "Preroll/Flush adversarial sink", "GstPipeStudio");
        addDefaultPorts(b, gps::elem::Caps::any());
    }

    bool start() override {
        clockSync().setEnabled(false);
        return SinkElement::start();
    }

    GstFlowReturn consume(gps::elem::Buffer& /*buf*/) override {
        g_consumeCount.fetch_add(1, std::memory_order_relaxed);
        return GST_FLOW_OK;
    }

    static inline std::atomic<int> g_consumeCount{0};
};

// 3. VideoFilterElement for sticky event ordering & no duplicate STREAM_START/CAPS test
class AdvFilter : public gps::elem::VideoFilterElement {
public:
    explicit AdvFilter(GstElement* raw) : VideoFilterElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Adv Filter", "Filter/Effect/Video", "Sticky events adversarial filter", "GstPipeStudio");
        gps::elem::VideoRawSpec spec;
        spec.formats = {"GRAY8"};
        addDefaultPorts(b, spec, spec);
    }

    GstFlowReturn filterInPlace(gps::elem::VideoFrame& /*frame*/) override {
        return GST_FLOW_OK;
    }
};

// 4. BarrierElement for PtsWindow + FLUSH_START/FLUSH_STOP concurrency test
class AdvBarrier : public gps::elem::Element {
public:
    explicit AdvBarrier(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Adv Barrier", "Filter/Test", "Barrier adversarial test", "GstPipeStudio");
        b.addSinkPort("sink_0", gps::elem::Caps::any(), gps::elem::Presence::Always,
                      gps::elem::InputMode::BoundedQueue, 4, gps::elem::OverflowPolicy::DropOldest);
        b.addSinkPort("sink_1", gps::elem::Caps::any(), gps::elem::Presence::Always,
                      gps::elem::InputMode::BoundedQueue, 4, gps::elem::OverflowPolicy::DropOldest);
        b.addSrcPort("src", gps::elem::Caps::any(), gps::elem::Presence::Always);
    }

    bool open() override {
        auto* s0 = sinkPort("sink_0");
        auto* s1 = sinkPort("sink_1");
        if (!s0 || !s1) return false;
        m_barrier.setPorts({s0, s1});
        m_barrier.setPolicy(gps::elem::PtsWindow{10 * GST_MSECOND, 50 * GST_MSECOND});
        return true;
    }

    bool start() override {
        m_barrier.resetUnblock();
        m_running.store(true, std::memory_order_release);
        m_thread = std::thread([this]() {
            std::vector<std::optional<gps::elem::Buffer>> bufs;
            while (m_running.load(std::memory_order_acquire)) {
                auto st = m_barrier.acquire(bufs);
                if (st == gps::elem::BarrierStatus::Ready) {
                    m_alignedCount.fetch_add(1, std::memory_order_relaxed);
                } else if (st == gps::elem::BarrierStatus::Eos) {
                    break;
                } else if (st == gps::elem::BarrierStatus::Flushing) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
        return true;
    }

    void unblock() override {
        m_running.store(false, std::memory_order_release);
        m_barrier.unblock();
    }

    bool stop() override {
        m_running.store(false, std::memory_order_release);
        m_barrier.unblock();
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return true;
    }

    std::atomic<int> m_alignedCount{0};

private:
    gps::elem::BarrierCoordinator m_barrier;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

struct EventLog {
    std::vector<GstEventType> types;
    std::mutex mutex;
};

GstPadProbeReturn eventProbeCb(GstPad* /*pad*/, GstPadProbeInfo* info, gpointer userData) {
    auto* log = static_cast<EventLog*>(userData);
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (ev) {
        std::lock_guard<std::mutex> lock(log->mutex);
        log->types.push_back(GST_EVENT_TYPE(ev));
    }
    return GST_PAD_PROBE_OK;
}

void testRequestPadNullNameAndRelease() {
    GstElement* elem = gst_element_factory_make("gpsadvsplitter", "advsplit");
    TEST_CHECK(elem != nullptr);

    GstPad* p0 = gst_element_request_pad_simple(elem, "src_%u");
    GstPad* p1 = gst_element_request_pad_simple(elem, "src_%u");
    TEST_CHECK(p0 != nullptr);
    TEST_CHECK(p1 != nullptr);
    TEST_CHECK(p0 != p1);

    gchar* n0 = gst_pad_get_name(p0);
    gchar* n1 = gst_pad_get_name(p1);
    TEST_CHECK(n0 && n1 && std::string(n0) != std::string(n1));
    g_free(n0);
    g_free(n1);

    auto* impl = reinterpret_cast<gps::elem::GpsElementInstance*>(elem)->impl;
    auto* adv = dynamic_cast<AdvSplitter*>(impl);
    TEST_CHECK(adv != nullptr);
    adv->linkAll();

    // Release p0, then iterate internal links on sink pad -> must only see p1 (no UAF!)
    gst_element_release_request_pad(elem, p0);
    gst_object_unref(p0);

    GstPad* sinkPad = gst_element_get_static_pad(elem, "sink");
    TEST_CHECK(sinkPad != nullptr);
    GstIterator* it = gst_pad_iterate_internal_links(sinkPad);
    TEST_CHECK(it != nullptr);
    GValue item = G_VALUE_INIT;
    int count = 0;
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        GstPad* linked = GST_PAD_CAST(g_value_get_object(&item));
        TEST_EQ(linked, p1);
        ++count;
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(it);
    TEST_EQ(count, 1);

    gst_element_release_request_pad(elem, p1);
    gst_object_unref(p1);
    gst_object_unref(sinkPad);
    gst_object_unref(elem);
}

void testSinkPrerollAndFlushClearsDeferredEos() {
    AdvSink::g_consumeCount.store(0);
    GstElement* pipe = gst_pipeline_new("adv_sink_pipe");
    GstElement* src = gst_element_factory_make("appsrc", "src");
    GstElement* sink = gst_element_factory_make("gpsadvsink", "sink");
    TEST_CHECK(pipe && src && sink);

    gst_bin_add_many(GST_BIN(pipe), src, sink, nullptr);
    TEST_CHECK(gst_element_link(src, sink));

    GstBus* bus = gst_element_get_bus(pipe);
    TEST_EQ(gst_element_set_state(pipe, GST_STATE_PAUSED), GST_STATE_CHANGE_ASYNC);

    // Push 1 buffer -> prerolls, but MUST NOT call consume() while in PAUSED!
    GstBuffer* b1 = gst_buffer_new_allocate(nullptr, 16, nullptr);
    GST_BUFFER_PTS(b1) = 0;
    GstFlowReturn fret = GST_FLOW_OK;
    g_signal_emit_by_name(src, "push-buffer", b1, &fret);
    gst_buffer_unref(b1);
    TEST_EQ(fret, GST_FLOW_OK);

    GstMessage* asyncMsg = gst_bus_timed_pop_filtered(bus, 2 * GST_SECOND, GST_MESSAGE_ASYNC_DONE);
    TEST_CHECK(asyncMsg != nullptr);
    gst_message_unref(asyncMsg);

    g_usleep(80000);
    TEST_EQ(AdvSink::g_consumeCount.load(), 0);

    // Defer EOS in PAUSED, then FLUSH_START + FLUSH_STOP -> must clear deferred EOS
    g_signal_emit_by_name(src, "end-of-stream", &fret);
    GstMessage* noEos = gst_bus_timed_pop_filtered(bus, 80 * GST_MSECOND, GST_MESSAGE_EOS);
    TEST_CHECK(noEos == nullptr);

    GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
    TEST_CHECK(gst_pad_send_event(sinkPad, gst_event_new_flush_start()));
    TEST_CHECK(gst_pad_send_event(sinkPad, gst_event_new_flush_stop(TRUE)));
    gst_object_unref(sinkPad);

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    // Deferred EOS was cleared by FLUSH_STOP, so no EOS should appear
    GstMessage* noEosAfterFlush = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, GST_MESSAGE_EOS);
    TEST_CHECK(noEosAfterFlush == nullptr);

    gst_object_unref(bus);
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
}

void testTransformStickyEventOrder() {
    GstElement* pipe = gst_pipeline_new("adv_filter_pipe");
    GstElement* src = gst_element_factory_make("videotestsrc", "src");
    GstElement* filter = gst_element_factory_make("gpsadvfilter", "filter");
    GstElement* sink = gst_element_factory_make("fakesink", "sink");
    TEST_CHECK(pipe && src && filter && sink);

    g_object_set(src, "num-buffers", 3, nullptr);
    gst_bin_add_many(GST_BIN(pipe), src, filter, sink, nullptr);

    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "GRAY8",
                                        "width", G_TYPE_INT, 32,
                                        "height", G_TYPE_INT, 32,
                                        nullptr);
    TEST_CHECK(gst_element_link_filtered(src, filter, caps));
    gst_caps_unref(caps);
    TEST_CHECK(gst_element_link(filter, sink));

    EventLog log;
    GstPad* filterSrc = gst_element_get_static_pad(filter, "src");
    gst_pad_add_probe(filterSrc, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, eventProbeCb, &log, nullptr);
    gst_object_unref(filterSrc);

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_EOS);

    {
        std::lock_guard<std::mutex> lock(log.mutex);
        int streamStartCount = 0;
        int capsCount = 0;
        int segmentCount = 0;
        int firstStreamStartIdx = -1;
        int firstCapsIdx = -1;
        int firstSegmentIdx = -1;
        for (size_t i = 0; i < log.types.size(); ++i) {
            if (log.types[i] == GST_EVENT_STREAM_START) {
                if (firstStreamStartIdx < 0) firstStreamStartIdx = static_cast<int>(i);
                ++streamStartCount;
            } else if (log.types[i] == GST_EVENT_CAPS) {
                if (firstCapsIdx < 0) firstCapsIdx = static_cast<int>(i);
                ++capsCount;
            } else if (log.types[i] == GST_EVENT_SEGMENT) {
                if (firstSegmentIdx < 0) firstSegmentIdx = static_cast<int>(i);
                ++segmentCount;
            }
        }
        TEST_EQ(streamStartCount, 1);
        TEST_EQ(capsCount, 1);
        TEST_EQ(segmentCount, 1);
        TEST_CHECK(firstStreamStartIdx >= 0 && firstCapsIdx > firstStreamStartIdx && firstSegmentIdx > firstCapsIdx);
    }

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
}

void testTaskDriverUnblockNoSpin() {
    gps::elem::TaskDriver driver;
    std::atomic<int> steps{0};
    TEST_CHECK(driver.start(nullptr, [&]() {
        steps.fetch_add(1, std::memory_order_relaxed);
        driver.waitFor(5 * GST_MSECOND);
    }));
    g_usleep(30000);
    driver.unblock();
    const int countAtUnblock = steps.load(std::memory_order_relaxed);
    g_usleep(40000);
    // After unblock, taskFunc pauses via gst_task_pause rather than busy-spinning
    TEST_CHECK(steps.load(std::memory_order_relaxed) <= countAtUnblock + 1);

    auto t0 = std::chrono::steady_clock::now();
    driver.stop();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    TEST_CHECK(elapsedMs < 500);
}

void testBarrierConcurrentFlushAndPts() {
    GstElement* elem = gst_element_factory_make("gpsadvbarrier", "advbarrier");
    TEST_CHECK(elem != nullptr);
    TEST_CHECK(gst_element_set_state(elem, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    GstPad* p0 = gst_element_get_static_pad(elem, "sink_0");
    GstPad* p1 = gst_element_get_static_pad(elem, "sink_1");
    TEST_CHECK(p0 && p1);

    auto pushFrames = [](GstPad* pad, GstClockTime stepNs) {
        gst_pad_send_event(pad, gst_event_new_stream_start("s"));
        GstSegment seg;
        gst_segment_init(&seg, GST_FORMAT_TIME);
        gst_pad_send_event(pad, gst_event_new_segment(&seg));
        for (int i = 0; i < 40; ++i) {
            if (i == 20) {
                gst_pad_send_event(pad, gst_event_new_flush_start());
                gst_pad_send_event(pad, gst_event_new_flush_stop(TRUE));
                gst_pad_send_event(pad, gst_event_new_segment(&seg));
            }
            GstBuffer* b = gst_buffer_new_allocate(nullptr, 8, nullptr);
            GST_BUFFER_PTS(b) = static_cast<GstClockTime>(i) * stepNs;
            gst_pad_chain(pad, b);
            g_usleep(500);
        }
        gst_pad_send_event(pad, gst_event_new_eos());
    };

    std::thread t0(pushFrames, p0, 10 * GST_MSECOND);
    std::thread t1(pushFrames, p1, 12 * GST_MSECOND);
    t0.join();
    t1.join();

    gst_object_unref(p0);
    gst_object_unref(p1);
    TEST_CHECK(gst_element_set_state(elem, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE);
    gst_object_unref(elem);
}

// 6. Multi-SrcPort element for concurrent GstFlowCombiner push test
class AdvMultiSrc : public gps::elem::Element {
public:
    explicit AdvMultiSrc(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Adv MultiSrc", "Source/Test", "Concurrent multi-src push test", "GstPipeStudio");
        b.addSrcPort("src_0", gps::elem::Caps::any(), gps::elem::Presence::Always);
        b.addSrcPort("src_1", gps::elem::Caps::any(), gps::elem::Presence::Always);
    }

    bool start() override {
        if (auto* s0 = srcPort("src_0")) {
            s0->setCaps(gps::elem::Caps::custom("application/x-test"));
        }
        if (auto* s1 = srcPort("src_1")) {
            s1->setCaps(gps::elem::Caps::custom("application/x-test"));
        }
        return true;
    }
};

void testConcurrentMultiSrcPortPush() {
    GstElement* pipe = gst_pipeline_new("multi-src-pipe");
    GstElement* src = gst_element_factory_make("gpsadvmultisrc", "multisrc");
    GstElement* sink0 = gst_element_factory_make("fakesink", "sink0");
    GstElement* sink1 = gst_element_factory_make("fakesink", "sink1");
    TEST_CHECK(pipe && src && sink0 && sink1);
    g_object_set(sink0, "sync", FALSE, nullptr);
    g_object_set(sink1, "sync", FALSE, nullptr);
    gst_bin_add_many(GST_BIN(pipe), src, sink0, sink1, nullptr);
    TEST_CHECK(gst_element_link_pads(src, "src_0", sink0, "sink"));
    TEST_CHECK(gst_element_link_pads(src, "src_1", sink1, "sink"));

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    auto* impl = reinterpret_cast<gps::elem::GpsElementInstance*>(src)->impl;
    TEST_CHECK(impl != nullptr);
    auto* p0 = impl->srcPort("src_0");
    auto* p1 = impl->srcPort("src_1");
    TEST_CHECK(p0 && p1);

    auto pushLoop = [](gps::elem::SrcPort* port) {
        for (int i = 0; i < 60; ++i) {
            gps::elem::Buffer buf = gps::elem::Buffer::allocate(16);
            buf.setPts(static_cast<GstClockTime>(i) * GST_MSECOND);
            GstFlowReturn ret = port->push(std::move(buf));
            TEST_EQ(ret, GST_FLOW_OK);
        }
        TEST_CHECK(port->pushEos());
    };

    std::thread t0(pushLoop, p0);
    std::thread t1(pushLoop, p1);
    t0.join();
    t1.join();

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
}

// 7. CustomMeta<T> with throwing copy constructor during tee COW
struct ThrowingMeta {
    int value = 0;
    bool throwOnCopy = false;

    ThrowingMeta(int v, bool t) : value(v), throwOnCopy(t) {}
    ThrowingMeta(const ThrowingMeta& other)
        : value(other.value), throwOnCopy(other.throwOnCopy) {
        if (throwOnCopy) {
            throw std::runtime_error("intentional copy failure");
        }
    }
    ThrowingMeta(ThrowingMeta&&) noexcept = default;
    ThrowingMeta& operator=(ThrowingMeta&&) noexcept = default;
};

void testCustomMetaThrowingCopyDuringTeeCow() {
    GstElement* pipe = gst_pipeline_new("meta-throw-pipe");
    GstElement* tee = gst_element_factory_make("tee", "tee");
    GstElement* q0 = gst_element_factory_make("queue", "q0");
    GstElement* q1 = gst_element_factory_make("queue", "q1");
    GstElement* sink0 = gst_element_factory_make("fakesink", "sink0");
    GstElement* sink1 = gst_element_factory_make("fakesink", "sink1");
    TEST_CHECK(pipe && tee && q0 && q1 && sink0 && sink1);
    g_object_set(sink0, "sync", FALSE, nullptr);
    g_object_set(sink1, "sync", FALSE, nullptr);
    gst_bin_add_many(GST_BIN(pipe), tee, q0, q1, sink0, sink1, nullptr);
    TEST_CHECK(gst_element_link_many(tee, q0, sink0, nullptr));
    TEST_CHECK(gst_element_link_many(tee, q1, sink1, nullptr));
    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    gps::elem::Buffer buf = gps::elem::Buffer::allocate(32);
    auto* attached = gps::elem::CustomMeta<ThrowingMeta>::attach(buf, ThrowingMeta{99, true});
    TEST_CHECK(attached != nullptr);

    // Direct COW copy (simulates tee + downstream gst_buffer_make_writable)
    GstBuffer* sharedRef = gst_buffer_ref(buf.get());
    GstBuffer* copy = gst_buffer_copy(buf.get());
    TEST_CHECK(copy != nullptr);
    // Throwing copy must be caught cleanly inside transformFunc without std::terminate;
    // the failed transform means the copied buffer has no ThrowingMeta attached.
    gps::elem::Buffer copyWrap = gps::elem::Buffer::adopt(copy);
    TEST_CHECK(gps::elem::CustomMeta<ThrowingMeta>::get(copyWrap) == nullptr);
    TEST_CHECK(gps::elem::CustomMeta<ThrowingMeta>::get(buf) != nullptr);
    gst_buffer_unref(sharedRef);

    // Also push through tee to verify pipeline stays healthy
    GstPad* teeSink = gst_element_get_static_pad(tee, "sink");
    TEST_CHECK(teeSink != nullptr);
    gst_pad_send_event(teeSink, gst_event_new_stream_start("meta-throw"));
    GstSegment seg;
    gst_segment_init(&seg, GST_FORMAT_TIME);
    gst_pad_send_event(teeSink, gst_event_new_segment(&seg));
    TEST_EQ(gst_pad_chain(teeSink, buf.release()), GST_FLOW_OK);
    gst_pad_send_event(teeSink, gst_event_new_eos());
    gst_object_unref(teeSink);

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
}

// 8. SinkElement preroll on GST_EVENT_GAP in PAUSED
void testSinkPrerollOnGapEvent() {
    GstElement* sink = gst_element_factory_make("gpsadvsink", "gapsink");
    TEST_CHECK(sink != nullptr);

    GstStateChangeReturn ret = gst_element_set_state(sink, GST_STATE_PAUSED);
    TEST_EQ(ret, GST_STATE_CHANGE_ASYNC);

    GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");
    TEST_CHECK(sinkPad != nullptr);
    TEST_CHECK(gst_pad_send_event(sinkPad, gst_event_new_stream_start("gap-stream")));
    GstSegment seg;
    gst_segment_init(&seg, GST_FORMAT_TIME);
    TEST_CHECK(gst_pad_send_event(sinkPad, gst_event_new_segment(&seg)));
    TEST_CHECK(gst_pad_send_event(sinkPad, gst_event_new_gap(0, 33 * GST_MSECOND)));

    GstState state = GST_STATE_VOID_PENDING;
    GstState pending = GST_STATE_VOID_PENDING;
    TEST_EQ(gst_element_get_state(sink, &state, &pending, GST_SECOND), GST_STATE_CHANGE_SUCCESS);
    TEST_EQ(state, GST_STATE_PAUSED);

    gst_object_unref(sinkPad);
    TEST_CHECK(gst_element_set_state(sink, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE);
    gst_object_unref(sink);
}

// 9. SinkPort setPullModePreferred(true) falling back to PUSH
class AdvPullPreferredSink : public gps::elem::SinkElement {
public:
    explicit AdvPullPreferredSink(GstElement* raw) : SinkElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Adv PullFallback Sink", "Sink/Test", "Pull preferred fallback to push", "GstPipeStudio");
        addDefaultPorts(b, gps::elem::Caps::any());
    }

    bool open() override {
        if (auto* sink = sinkPort("sink")) {
            sink->setPullModePreferred(true);
        }
        return SinkElement::open();
    }

    GstFlowReturn consume(gps::elem::Buffer& /*buf*/) override {
        m_count.fetch_add(1, std::memory_order_relaxed);
        return GST_FLOW_OK;
    }

    int count() const noexcept { return m_count.load(std::memory_order_relaxed); }

private:
    std::atomic<int> m_count{0};
};

void testSinkPullPreferredFallbackToPush() {
    GstElement* pipe = gst_pipeline_new("pull-fallback-pipe");
    GstElement* src = gst_element_factory_make("videotestsrc", "vsrc");
    GstElement* sink = gst_element_factory_make("gpsadvpullsink", "psink");
    TEST_CHECK(pipe && src && sink);
    g_object_set(src, "num-buffers", 2, nullptr);
    gst_bin_add_many(GST_BIN(pipe), src, sink, nullptr);
    TEST_CHECK(gst_element_link(src, sink));

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    GstBus* bus = gst_element_get_bus(pipe);
    GstMessage* msg = gst_bus_timed_pop_filtered(
        bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    TEST_CHECK(msg != nullptr);
    TEST_EQ(GST_MESSAGE_TYPE(msg), GST_MESSAGE_EOS);
    gst_message_unref(msg);
    gst_object_unref(bus);

    auto* impl = dynamic_cast<AdvPullPreferredSink*>(
        reinterpret_cast<gps::elem::GpsElementInstance*>(sink)->impl);
    TEST_CHECK(impl != nullptr);
    TEST_EQ(GST_PAD_MODE(impl->sinkPort("sink")->pad()), GST_PAD_MODE_PUSH);
    TEST_EQ(impl->count(), 2);

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
}

// 10. Element::handleChangeState play() failure rollback
class AdvPlayFailElement : public gps::elem::Element {
public:
    explicit AdvPlayFailElement(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Adv PlayFail", "Generic/Test", "Play failure rollback test", "GstPipeStudio");
    }

    bool play() override {
        return false;
    }

    bool pause() override {
        m_pauseCalled.store(true, std::memory_order_release);
        return true;
    }

    bool pauseCalled() const noexcept { return m_pauseCalled.load(std::memory_order_acquire); }

private:
    std::atomic<bool> m_pauseCalled{false};
};

void testPlayFailureRollback() {
    GstElement* elem = gst_element_factory_make("gpsadvplayfail", "playfail");
    TEST_CHECK(elem != nullptr);
    TEST_EQ(gst_element_set_state(elem, GST_STATE_PAUSED), GST_STATE_CHANGE_SUCCESS);
    TEST_EQ(gst_element_set_state(elem, GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);

    auto* impl = dynamic_cast<AdvPlayFailElement*>(
        reinterpret_cast<gps::elem::GpsElementInstance*>(elem)->impl);
    TEST_CHECK(impl != nullptr);
    TEST_CHECK(impl->pauseCalled());

    GstState state = GST_STATE_VOID_PENDING;
    GstState pending = GST_STATE_VOID_PENDING;
    gst_element_get_state(elem, &state, &pending, 0);
    TEST_EQ(state, GST_STATE_PAUSED);

    TEST_CHECK(gst_element_set_state(elem, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE);
    gst_object_unref(elem);
}

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<AdvSplitter>("gpsadvsplitter"));
    TEST_CHECK(gps::elem::registerElementStatic<AdvSink>("gpsadvsink"));
    TEST_CHECK(gps::elem::registerElementStatic<AdvFilter>("gpsadvfilter"));
    TEST_CHECK(gps::elem::registerElementStatic<AdvBarrier>("gpsadvbarrier"));
    TEST_CHECK(gps::elem::registerElementStatic<AdvMultiSrc>("gpsadvmultisrc"));
    TEST_CHECK(gps::elem::registerElementStatic<AdvPullPreferredSink>("gpsadvpullsink"));
    TEST_CHECK(gps::elem::registerElementStatic<AdvPlayFailElement>("gpsadvplayfail"));

    testRequestPadNullNameAndRelease();
    testSinkPrerollAndFlushClearsDeferredEos();
    testTransformStickyEventOrder();
    testTaskDriverUnblockNoSpin();
    testBarrierConcurrentFlushAndPts();
    testConcurrentMultiSrcPortPush();
    testCustomMetaThrowingCopyDuringTeeCow();
    testSinkPrerollOnGapEvent();
    testSinkPullPreferredFallbackToPush();
    testPlayFailureRollback();

    return 0;
}
