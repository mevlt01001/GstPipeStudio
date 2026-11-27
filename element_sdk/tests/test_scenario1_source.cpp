#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <atomic>
#include <chrono>

namespace {

class TestSource : public gps::elem::SourceElement {
public:
    explicit TestSource(GstElement* raw) : SourceElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Test Source", "Source/Test", "Autonomous 0->1 source", "GstPipeStudio");
        addDefaultPorts(b, gps::elem::Caps::custom("application/x-test"));
        b.properties.prop("num-buffers", &TestSource::m_numBuffers, 5, -1, 10000)
            .mutability(gps::elem::Mutability::Ready);
        b.properties.prop("is-live", &TestSource::m_live, false)
            .mutability(gps::elem::Mutability::Null)
            .onChange<TestSource>([](TestSource& self) {
                self.setLiveSource(self.m_live);
            });
        b.properties.prop("sleep-ms", &TestSource::m_sleepMs, 0, 0, 60000)
            .mutability(gps::elem::Mutability::Ready);
    }

    bool start() override {
        m_produced = 0;
        if (auto* src = srcPort("src")) {
            src->setCaps(gps::elem::Caps::custom("application/x-test"));
        }
        return SourceElement::start();
    }

    GstFlowReturn produce(gps::elem::SrcPort& src) override {
        if (m_sleepMs > 0) {
            if (!driver().waitFor(static_cast<GstClockTime>(m_sleepMs) * GST_MSECOND)) {
                return GST_FLOW_FLUSHING;
            }
        }
        if (m_numBuffers >= 0 && m_produced >= m_numBuffers) {
            return GST_FLOW_EOS;
        }
        auto buf = gps::elem::Buffer::fromPod<int32_t>(m_produced);
        buf.setPts(static_cast<GstClockTime>(m_produced) * 33 * GST_MSECOND);
        buf.setDuration(33 * GST_MSECOND);
        ++m_produced;
        return src.push(std::move(buf));
    }

private:
    int32_t m_numBuffers = 5;
    bool m_live = false;
    int32_t m_sleepMs = 0;
    int32_t m_produced = 0;
};

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<TestSource>("gpstestsrc"));

    // 1. Finite EOS test
    {
        GstElement* pipe = gst_pipeline_new("finite");
        GstElement* src = gst_element_factory_make("gpstestsrc", "src");
        GstElement* sink = gst_element_factory_make("fakesink", "sink");
        TEST_CHECK(pipe && src && sink);

        g_object_set(src, "num-buffers", 5, nullptr);
        g_object_set(sink, "signal-handoffs", TRUE, nullptr);

        std::atomic<int> count{0};
        g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer*, GstPad*, gpointer ud) {
            static_cast<std::atomic<int>*>(ud)->fetch_add(1);
        }), &count);

        gst_bin_add_many(GST_BIN(pipe), src, sink, nullptr);
        TEST_CHECK(gst_element_link(src, sink));

        TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
        TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_EOS);
        TEST_EQ(count.load(), 5);

        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }

    // 2. Live source NO_PREROLL + instant PAUSED->READY unblock test
    {
        GstElement* pipe = gst_pipeline_new("live");
        GstElement* src = gst_element_factory_make("gpstestsrc", "src");
        GstElement* sink = gst_element_factory_make("fakesink", "sink");
        TEST_CHECK(pipe && src && sink);

        g_object_set(src, "is-live", TRUE, "num-buffers", -1, "sleep-ms", 10000, nullptr);
        gst_bin_add_many(GST_BIN(pipe), src, sink, nullptr);
        TEST_CHECK(gst_element_link(src, sink));

        GstStateChangeReturn ret = gst_element_set_state(pipe, GST_STATE_PAUSED);
        TEST_EQ(ret, GST_STATE_CHANGE_NO_PREROLL);

        auto t0 = std::chrono::steady_clock::now();
        TEST_CHECK(gst_element_set_state(pipe, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        TEST_CHECK(elapsedMs < 1000);

        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }

    return 0;
}
