#include "gps_elem/barrier.h"
#include "gps_elem/driver.h"
#include "gps_elem/element.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <atomic>
#include <vector>

namespace {

class TestBarrierJoin : public gps::elem::Element {
public:
    explicit TestBarrierJoin(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Test Barrier Join", "Filter/Muxer", "2->1 PtsWindow join", "GstPipeStudio");
        b.addSinkPort("sink_a", gps::elem::Caps::any(), gps::elem::Presence::Always, gps::elem::InputMode::BoundedQueue, 8, gps::elem::OverflowPolicy::Block);
        b.addSinkPort("sink_b", gps::elem::Caps::any(), gps::elem::Presence::Always, gps::elem::InputMode::BoundedQueue, 8, gps::elem::OverflowPolicy::Block);
        b.addSrcPort("src", gps::elem::Caps::any(), gps::elem::Presence::Always);
    }

    bool open() override {
        auto* a = sinkPort("sink_a");
        auto* b = sinkPort("sink_b");
        auto* src = srcPort("src");
        if (!a || !b || !src) return false;
        m_barrier.setPorts({a, b});
        m_barrier.setPolicy(gps::elem::PtsWindow{20 * GST_MSECOND, GST_CLOCK_TIME_NONE});
        return true;
    }

    bool start() override {
        m_barrier.resetUnblock();
        m_driver.setUnblockCallback([this]() { m_barrier.unblock(); });
        return m_driver.start(this, [this]() { step(); });
    }

    bool onSinkEvent(gps::elem::SinkPort& port, GstEvent* event) override {
        if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_START) {
            m_barrier.unblock();
        } else if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP) {
            auto* a = sinkPort("sink_a");
            auto* b = sinkPort("sink_b");
            if (a && b && !a->isFlushing() && !b->isFlushing()) {
                m_driver.stop();
                m_barrier.resetUnblock();
                m_driver.start(this, [this]() { step(); });
            }
        }
        return Element::onSinkEvent(port, event);
    }

    void unblock() override { m_driver.unblock(); }
    bool stop() override {
        m_driver.stop();
        return true;
    }

private:
    void step() {
        std::vector<std::optional<gps::elem::Buffer>> batch;
        auto status = m_barrier.acquire(batch);
        auto* src = srcPort("src");
        if (status == gps::elem::BarrierStatus::Eos) {
            src->pushEos();
            m_driver.pause();
            return;
        }
        if (status != gps::elem::BarrierStatus::Ready) {
            m_driver.pause();
            return;
        }
        if (batch[0].has_value() && batch[1].has_value()) {
            auto out = gps::elem::Buffer::allocate(8);
            out.setPts(batch[0]->pts());
            if (src->push(std::move(out)) != GST_FLOW_OK) {
                m_driver.pause();
            }
        }
    }

    gps::elem::BarrierCoordinator m_barrier;
    gps::elem::TaskDriver m_driver;
};

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<TestBarrierJoin>("gpstestbarrier"));

    GstElement* pipe = gst_pipeline_new("barrier_test");
    GstElement* srcA = gst_element_factory_make("appsrc", "srcA");
    GstElement* srcB = gst_element_factory_make("appsrc", "srcB");
    GstElement* join = gst_element_factory_make("gpstestbarrier", "join");
    GstElement* sink = gst_element_factory_make("fakesink", "sink");
    TEST_CHECK(pipe && srcA && srcB && join && sink);

    g_object_set(srcA, "format", GST_FORMAT_TIME, nullptr);
    g_object_set(srcB, "format", GST_FORMAT_TIME, nullptr);
    g_object_set(sink, "signal-handoffs", TRUE, "async", FALSE, nullptr);

    std::atomic<int> count{0};
    g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer*, GstPad*, gpointer ud) {
        static_cast<std::atomic<int>*>(ud)->fetch_add(1);
    }), &count);

    gst_bin_add_many(GST_BIN(pipe), srcA, srcB, join, sink, nullptr);
    TEST_CHECK(gst_element_link_pads(srcA, "src", join, "sink_a"));
    TEST_CHECK(gst_element_link_pads(srcB, "src", join, "sink_b"));
    TEST_CHECK(gst_element_link_pads(join, "src", sink, "sink"));

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    auto push = [](GstElement* app, GstClockTime pts) {
        auto b = gps::elem::Buffer::allocate(4);
        b.setPts(pts);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(app, "push-buffer", b.get(), &ret);
        return ret;
    };

    // Matched pairs: (0ms, 10ms), (100ms, 105ms)
    TEST_EQ(push(srcA, 0), GST_FLOW_OK);
    TEST_EQ(push(srcB, 10 * GST_MSECOND), GST_FLOW_OK);
    TEST_EQ(push(srcA, 100 * GST_MSECOND), GST_FLOW_OK);
    TEST_EQ(push(srcB, 105 * GST_MSECOND), GST_FLOW_OK);

    g_usleep(50000);
    TEST_EQ(count.load(), 2);

    // Mid-stream FLUSH_START / FLUSH_STOP without deadlock
    GstPad* padA = gst_element_get_static_pad(join, "sink_a");
    GstPad* padB = gst_element_get_static_pad(join, "sink_b");
    gst_pad_send_event(padA, gst_event_new_flush_start());
    gst_pad_send_event(padB, gst_event_new_flush_start());
    gst_pad_send_event(padA, gst_event_new_flush_stop(TRUE));
    gst_pad_send_event(padB, gst_event_new_flush_stop(TRUE));
    gst_object_unref(padA);
    gst_object_unref(padB);

    // Post-flush matched pair
    TEST_EQ(push(srcA, 200 * GST_MSECOND), GST_FLOW_OK);
    TEST_EQ(push(srcB, 205 * GST_MSECOND), GST_FLOW_OK);

    GstFlowReturn eosRet = GST_FLOW_OK;
    g_signal_emit_by_name(srcA, "end-of-stream", &eosRet);
    g_signal_emit_by_name(srcB, "end-of-stream", &eosRet);

    TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_EOS);
    TEST_EQ(count.load(), 3);

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    return 0;
}
