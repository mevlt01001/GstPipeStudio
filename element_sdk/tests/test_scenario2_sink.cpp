#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <atomic>
#include <chrono>

namespace {

class TestSink : public gps::elem::SinkElement {
public:
    explicit TestSink(GstElement* raw) : SinkElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Test Sink", "Sink/Test", "Terminal 1->0 sink", "GstPipeStudio");
        addDefaultPorts(b, gps::elem::Caps::any());
        b.properties.prop("sync", &TestSink::m_sync, true)
            .mutability(gps::elem::Mutability::Playing)
            .onChange<TestSink>([](TestSink& s) { s.clockSync().setEnabled(s.m_sync); });
    }

    bool start() override {
        clockSync().setEnabled(m_sync);
        return SinkElement::start();
    }

    GstFlowReturn consume(gps::elem::Buffer& /*buf*/) override {
        g_receivedCount.fetch_add(1);
        return GST_FLOW_OK;
    }

    static inline std::atomic<int> g_receivedCount{0};

private:
    bool m_sync = true;
};

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<TestSink>("gpstestsink"));

    // 1. Preroll ASYNC -> ASYNC_DONE + Deferred EOS until PLAYING
    {
        TestSink::g_receivedCount.store(0);
        GstElement* pipe = gst_pipeline_new("sink_preroll");
        GstElement* src = gst_element_factory_make("appsrc", "src");
        GstElement* sink = gst_element_factory_make("gpstestsink", "sink");
        TEST_CHECK(pipe && src && sink);
        TEST_CHECK(GST_OBJECT_FLAG_IS_SET(sink, GST_ELEMENT_FLAG_SINK));

        g_object_set(sink, "sync", FALSE, nullptr);
        gst_bin_add_many(GST_BIN(pipe), src, sink, nullptr);
        TEST_CHECK(gst_element_link(src, sink));

        GstBus* bus = gst_element_get_bus(pipe);
        GstStateChangeReturn ret = gst_element_set_state(pipe, GST_STATE_PAUSED);
        TEST_EQ(ret, GST_STATE_CHANGE_ASYNC);

        // Push 1 buffer -> should trigger ASYNC_DONE
        GstBuffer* b1 = gst_buffer_new_allocate(nullptr, 16, nullptr);
        GST_BUFFER_PTS(b1) = 0;
        GstFlowReturn fret = GST_FLOW_OK;
        g_signal_emit_by_name(src, "push-buffer", b1, &fret);
        gst_buffer_unref(b1);
        TEST_EQ(fret, GST_FLOW_OK);

        GstMessage* asyncMsg = gst_bus_timed_pop_filtered(bus, 2 * GST_SECOND, GST_MESSAGE_ASYNC_DONE);
        TEST_CHECK(asyncMsg != nullptr);
        gst_message_unref(asyncMsg);

        // Send EOS while still in PAUSED -> must be deferred, NOT posted immediately!
        g_signal_emit_by_name(src, "end-of-stream", &fret);
        GstMessage* noEos = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, GST_MESSAGE_EOS);
        TEST_CHECK(noEos == nullptr);

        // Transition to PLAYING -> deferred EOS must now appear!
        TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        GstMessage* eosMsg = gst_bus_timed_pop_filtered(bus, 2 * GST_SECOND, GST_MESSAGE_EOS);
        TEST_CHECK(eosMsg != nullptr);
        gst_message_unref(eosMsg);
        TEST_EQ(TestSink::g_receivedCount.load(), 1);

        gst_object_unref(bus);
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }

    // 2. ClockSync wakeup on early stop
    {
        GstElement* pipe = gst_pipeline_new("sink_clocksync");
        GstElement* src = gst_element_factory_make("appsrc", "src");
        GstElement* sink = gst_element_factory_make("gpstestsink", "sink");
        TEST_CHECK(pipe && src && sink);

        g_object_set(sink, "sync", TRUE, nullptr);
        g_object_set(src, "format", GST_FORMAT_TIME, nullptr);
        gst_bin_add_many(GST_BIN(pipe), src, sink, nullptr);
        TEST_CHECK(gst_element_link(src, sink));

        TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

        // Push buffer 10s in the future so sink's ClockSync blocks in gst_clock_id_wait
        GstBuffer* futureBuf = gst_buffer_new_allocate(nullptr, 16, nullptr);
        GST_BUFFER_PTS(futureBuf) = 10 * GST_SECOND;
        GstFlowReturn fret = GST_FLOW_OK;
        g_signal_emit_by_name(src, "push-buffer", futureBuf, &fret);
        gst_buffer_unref(futureBuf);

        g_usleep(50000);  // 50ms to let sink enter gst_clock_id_wait
        auto t0 = std::chrono::steady_clock::now();
        TEST_CHECK(gst_element_set_state(pipe, GST_STATE_NULL) != GST_STATE_CHANGE_FAILURE);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        TEST_CHECK(elapsedMs < 1000);

        gst_object_unref(pipe);
    }

    return 0;
}
