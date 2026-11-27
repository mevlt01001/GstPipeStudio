#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <algorithm>
#include <atomic>
#include <vector>

namespace {

class TestReticleOverlay : public gps::elem::Element {
public:
    explicit TestReticleOverlay(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Test ReticleOverlay", "Filter/Video", "2->1 Mailbox overlay", "GstPipeStudio");
        gps::elem::VideoRawSpec vidSpec;
        vidSpec.formats = {"GRAY8"};
        b.addSinkPort("sink_video", gps::elem::Caps::videoRaw(vidSpec), gps::elem::Presence::Always, gps::elem::InputMode::BoundedQueue, 8, gps::elem::OverflowPolicy::Block);
        b.addSinkPort("sink_param", gps::elem::Caps::custom("application/x-float"), gps::elem::Presence::Always, gps::elem::InputMode::Mailbox);
        b.addSrcPort("src", gps::elem::Caps::videoRaw(vidSpec), gps::elem::Presence::Always);
    }

    bool open() override {
        auto* vid = sinkPort("sink_video");
        auto* param = sinkPort("sink_param");
        auto* src = srcPort("src");
        if (!vid || !param || !src) return false;
        linkInternal(*vid, *src);
        param->setDefault(gps::elem::Buffer::fromPod(0.5f));
        return true;
    }

    bool onSinkCaps(gps::elem::SinkPort& port, const gps::elem::Caps& caps) override {
        if (port.name() == "sink_video") {
            m_info = gps::elem::VideoInfo::fromCaps(caps);
            if (auto* src = srcPort("src")) {
                src->setCaps(caps);
            }
        }
        return true;
    }

    bool start() override {
        auto* vid = sinkPort("sink_video");
        auto* param = sinkPort("sink_param");
        auto* src = srcPort("src");
        m_driver.setUnblockCallback([vid]() { vid->unblock(); });
        return m_driver.start(this, [this, vid, param, src]() {
            auto bufOpt = vid->pop(GST_CLOCK_TIME_NONE);
            if (!bufOpt.has_value()) {
                if (vid->isEos()) {
                    src->pushEos();
                }
                m_driver.pause();
                return;
            }
            float alpha = 0.5f;
            if (auto p = param->latest()) {
                if (auto v = p->asPod<float>()) {
                    alpha = std::clamp(*v, 0.0f, 1.0f);
                }
            }
            auto outBuf = std::move(*bufOpt);
            if (m_info.has_value()) {
                if (auto frame = gps::elem::VideoFrame::mapWrite(*m_info, outBuf)) {
                    frame->row(0, 0)[0] = static_cast<uint8_t>(255.0f * alpha);
                    frame->unmap();
                }
            }
            if (src->push(std::move(outBuf)) != GST_FLOW_OK) {
                m_driver.pause();
            }
        });
    }

    void unblock() override { m_driver.unblock(); }
    bool stop() override {
        m_driver.stop();
        return true;
    }

private:
    std::optional<gps::elem::VideoInfo> m_info;
    gps::elem::TaskDriver m_driver;
};

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<TestReticleOverlay>("gpstestreticle"));

    GstElement* pipe = gst_pipeline_new("mailbox_test");
    GstElement* vidSrc = gst_element_factory_make("appsrc", "vidSrc");
    GstElement* paramSrc = gst_element_factory_make("appsrc", "paramSrc");
    GstElement* overlay = gst_element_factory_make("gpstestreticle", "overlay");
    GstElement* sink = gst_element_factory_make("fakesink", "sink");
    TEST_CHECK(pipe && vidSrc && paramSrc && overlay && sink);

    auto vidCaps = gps::elem::Caps::videoRaw({{"GRAY8"}, 8, 8, 30, 1, {}});
    auto paramCaps = gps::elem::Caps::custom("application/x-float");
    g_object_set(vidSrc, "caps", vidCaps.get(), "format", GST_FORMAT_TIME, nullptr);
    g_object_set(paramSrc, "caps", paramCaps.get(), "format", GST_FORMAT_TIME, nullptr);
    g_object_set(sink, "signal-handoffs", TRUE, "async", FALSE, nullptr);

    std::mutex vecMtx;
    std::vector<uint8_t> observedPixels;
    g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer* buf, GstPad*, gpointer ud) {
        auto* vec = static_cast<std::vector<uint8_t>*>(ud);
        GstMapInfo info{};
        if (gst_buffer_map(buf, &info, GST_MAP_READ)) {
            if (info.size > 0) {
                vec->push_back(info.data[0]);
            }
            gst_buffer_unmap(buf, &info);
        }
    }), &observedPixels);
    (void)vecMtx;

    gst_bin_add_many(GST_BIN(pipe), vidSrc, paramSrc, overlay, sink, nullptr);
    TEST_CHECK(gst_element_link_pads(vidSrc, "src", overlay, "sink_video"));
    TEST_CHECK(gst_element_link_pads(paramSrc, "src", overlay, "sink_param"));
    TEST_CHECK(gst_element_link_pads(overlay, "src", sink, "sink"));

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    // Push 2 frames with 0 packets on sink_param -> should use default 0.5f -> 127
    for (int i = 0; i < 2; ++i) {
        auto b = gps::elem::Buffer::allocate(64);
        b.setPts(static_cast<GstClockTime>(i) * 33 * GST_MSECOND);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(vidSrc, "push-buffer", b.get(), &ret);
        TEST_EQ(ret, GST_FLOW_OK);
    }

    // Give overlay task a moment to process first 2 frames before updating param
    g_usleep(50000);

    // Now push 1.0f onto sink_param -> updates live to 255
    {
        auto pb = gps::elem::Buffer::fromPod(1.0f);
        pb.setPts(66 * GST_MSECOND);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(paramSrc, "push-buffer", pb.get(), &ret);
        TEST_EQ(ret, GST_FLOW_OK);
    }
    g_usleep(20000);

    for (int i = 2; i < 4; ++i) {
        auto b = gps::elem::Buffer::allocate(64);
        b.setPts(static_cast<GstClockTime>(i) * 33 * GST_MSECOND);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(vidSrc, "push-buffer", b.get(), &ret);
        TEST_EQ(ret, GST_FLOW_OK);
    }

    // Only sink_video sends EOS (sink_param stays open!)
    GstFlowReturn eosRet = GST_FLOW_OK;
    g_signal_emit_by_name(vidSrc, "end-of-stream", &eosRet);

    TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_EOS);
    TEST_EQ(observedPixels.size(), static_cast<size_t>(4));
    TEST_EQ(observedPixels[0], static_cast<uint8_t>(127));
    TEST_EQ(observedPixels[1], static_cast<uint8_t>(127));
    TEST_EQ(observedPixels[2], static_cast<uint8_t>(255));
    TEST_EQ(observedPixels[3], static_cast<uint8_t>(255));

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    return 0;
}
