#include "gps_elem/element.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <atomic>
#include <cstdint>

namespace {

struct MevoFrameStats {
    uint64_t frameIndex;
    double meanLuma;
};

class MevoSplitter : public gps::elem::Element {
public:
    explicit MevoSplitter(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Mevo Splitter", "Filter/Splitter", "1->2+Sometimes splitter with custom POD", "GstPipeStudio");
        b.addSinkPort("sink", gps::elem::Caps::any(), gps::elem::Presence::Always, gps::elem::InputMode::Direct);
        b.addSrcPort("src_stats", gps::elem::Caps::custom("application/x-mevo"), gps::elem::Presence::Always);
        b.addSrcPort("src_pass", gps::elem::Caps::any(), gps::elem::Presence::Always);
        b.addSrcPort("src_alert", gps::elem::Caps::custom("application/x-mevo"), gps::elem::Presence::Sometimes);
    }

    bool open() override {
        auto* sink = sinkPort("sink");
        auto* stats = srcPort("src_stats");
        auto* pass = srcPort("src_pass");
        if (!sink || !stats || !pass) {
            return false;
        }
        linkInternal(*sink, *pass);
        stats->setCaps(gps::elem::Caps::custom("application/x-mevo"));

        sink->setDirectCallback([this, stats, pass](gps::elem::Buffer buf) -> GstFlowReturn {
            uint8_t firstByte = 0;
            if (auto r = buf.read(); r.valid() && r.size() > 0) {
                firstByte = r.data()[0];
            }

            MevoFrameStats s{m_frameIdx++, static_cast<double>(firstByte)};
            auto statsBuf = gps::elem::Buffer::fromPod(s);
            statsBuf.setPts(buf.pts());

            if (!m_alertCreated && firstByte >= 200) {
                m_alertCreated = true;
                auto& alertPort = addSometimesSrcPort("src_alert", gps::elem::Caps::custom("application/x-mevo"));
                alertPort.setCaps(gps::elem::Caps::custom("application/x-mevo"));
                noMorePads();
                auto alertBuf = gps::elem::Buffer::fromPod(s);
                alertBuf.setPts(buf.pts());
                alertPort.push(std::move(alertBuf));
            }

            GstFlowReturn r1 = stats->push(std::move(statsBuf));
            GstFlowReturn r2 = pass->push(std::move(buf));
            // GstFlowCombiner combines both pads; if one is unlinked and the other is OK, combined is OK.
            if (r1 == GST_FLOW_OK || r2 == GST_FLOW_OK) {
                return GST_FLOW_OK;
            }
            return r1;
        });
        return true;
    }

private:
    uint64_t m_frameIdx = 0;
    bool m_alertCreated = false;
};

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<MevoSplitter>("gpsmevosplit"));

    GstElement* pipe = gst_pipeline_new("splitter");
    GstElement* src = gst_element_factory_make("appsrc", "src");
    GstElement* split = gst_element_factory_make("gpsmevosplit", "split");
    GstElement* sinkStats = gst_element_factory_make("fakesink", "sinkStats");
    GstElement* sinkAlert = gst_element_factory_make("fakesink", "sinkAlert");
    TEST_CHECK(pipe && src && split && sinkStats && sinkAlert);

    g_object_set(sinkStats, "signal-handoffs", TRUE, "async", FALSE, nullptr);
    g_object_set(sinkAlert, "signal-handoffs", TRUE, "async", FALSE, nullptr);

    struct State {
        std::atomic<int> statsCount{0};
        std::atomic<int> alertCount{0};
        std::atomic<bool> padAddedFired{false};
        std::atomic<bool> podValid{true};
        GstElement* sinkAlert = nullptr;
    } st;
    st.sinkAlert = sinkAlert;

    g_signal_connect(sinkStats, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer* buf, GstPad*, gpointer ud) {
        auto* s = static_cast<State*>(ud);
        auto wrapped = gps::elem::Buffer::borrow(buf);
        auto pod = wrapped.asPod<MevoFrameStats>();
        if (!pod.has_value()) {
            s->podValid.store(false);
        }
        s->statsCount.fetch_add(1);
    }), &st);

    g_signal_connect(sinkAlert, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer*, GstPad*, gpointer ud) {
        static_cast<State*>(ud)->alertCount.fetch_add(1);
    }), &st);

    g_signal_connect(split, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* newPad, gpointer ud) {
        auto* s = static_cast<State*>(ud);
        gchar* name = gst_pad_get_name(newPad);
        if (name && std::string(name) == "src_alert") {
            s->padAddedFired.store(true);
            GstPad* sinkPad = gst_element_get_static_pad(s->sinkAlert, "sink");
            gst_pad_link(newPad, sinkPad);
            gst_object_unref(sinkPad);
        }
        g_free(name);
    }), &st);

    // Note: src_pass is intentionally left UNLINKED to test GstFlowCombiner!
    gst_bin_add_many(GST_BIN(pipe), src, split, sinkStats, sinkAlert, nullptr);
    TEST_CHECK(gst_element_link_pads(src, "src", split, "sink"));
    TEST_CHECK(gst_element_link_pads(split, "src_stats", sinkStats, "sink"));

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    // Frame 0: value 50 (no alert)
    {
        auto buf = gps::elem::Buffer::allocate(4);
        buf.write().data()[0] = 50;
        buf.setPts(0);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(src, "push-buffer", buf.get(), &ret);
        TEST_EQ(ret, GST_FLOW_OK);
    }
    // Frame 1: value 220 (triggers Sometimes src_alert!)
    {
        auto buf = gps::elem::Buffer::allocate(4);
        buf.write().data()[0] = 220;
        buf.setPts(33 * GST_MSECOND);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(src, "push-buffer", buf.get(), &ret);
        TEST_EQ(ret, GST_FLOW_OK);
    }
    GstFlowReturn eosRet = GST_FLOW_OK;
    g_signal_emit_by_name(src, "end-of-stream", &eosRet);

    TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_EOS);
    TEST_EQ(st.statsCount.load(), 2);
    TEST_CHECK(st.podValid.load());
    TEST_CHECK(st.padAddedFired.load());
    TEST_EQ(st.alertCount.load(), 1);

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    return 0;
}
