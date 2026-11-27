#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <atomic>
#include <cstring>

namespace {

class TestThresholdFilter : public gps::elem::VideoFilterElement {
public:
    explicit TestThresholdFilter(GstElement* raw) : VideoFilterElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Test Threshold", "Filter/Video", "In-place GRAY8 threshold", "GstPipeStudio");
        gps::elem::VideoRawSpec spec;
        spec.formats = {"GRAY8"};
        addDefaultPorts(b, spec, spec);
        b.properties.prop("threshold", &TestThresholdFilter::m_threshold, 128, 0, 255);
    }

    GstFlowReturn filterInPlace(gps::elem::VideoFrame& frame) override {
        const uint8_t t = static_cast<uint8_t>(m_threshold);
        for (int y = 0; y < frame.height(); ++y) {
            uint8_t* row = frame.row(0, y);
            for (int x = 0; x < frame.width(); ++x) {
                row[x] = (row[x] >= t) ? 255 : 0;
            }
        }
        return GST_FLOW_OK;
    }

private:
    int32_t m_threshold = 128;
};

class TestRgbaToGrayFilter : public gps::elem::VideoFilterElement {
public:
    explicit TestRgbaToGrayFilter(GstElement* raw) : VideoFilterElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("RGBA to GRAY8", "Filter/Converter/Video", "Out-of-place RGBA->GRAY8", "GstPipeStudio");
        gps::elem::VideoRawSpec inSpec;
        inSpec.formats = {"RGBA"};
        gps::elem::VideoRawSpec outSpec;
        outSpec.formats = {"GRAY8"};
        addDefaultPorts(b, inSpec, outSpec);
    }

    gps::elem::Caps transformCaps(GstPadDirection dir, const gps::elem::Caps& caps, const gps::elem::Caps& filter) override {
        if (!caps.isValid()) {
            return filter.isValid() ? filter : gps::elem::Caps::any();
        }
        GstCaps* copy = gst_caps_copy(caps.get());
        for (guint i = 0; i < gst_caps_get_size(copy); ++i) {
            GstStructure* s = gst_caps_get_structure(copy, i);
            gst_structure_set(s, "format", G_TYPE_STRING, (dir == GST_PAD_SINK) ? "GRAY8" : "RGBA", nullptr);
        }
        gps::elem::Caps out = gps::elem::Caps::adopt(copy);
        if (filter.isValid()) {
            out = out.intersect(filter);
        }
        return out;
    }

    GstFlowReturn filterOutOfPlace(const gps::elem::VideoFrame& inFrame, gps::elem::VideoFrame& outFrame) override {
        for (int y = 0; y < inFrame.height(); ++y) {
            const uint8_t* inRow = inFrame.row(0, y);
            uint8_t* outRow = outFrame.row(0, y);
            for (int x = 0; x < inFrame.width(); ++x) {
                const uint8_t* px = inRow + x * 4;
                outRow[x] = static_cast<uint8_t>((px[0] + px[1] + px[2]) / 3);
            }
        }
        return GST_FLOW_OK;
    }
};

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<TestThresholdFilter>("gpstestthresh"));
    TEST_CHECK(gps::elem::registerElementStatic<TestRgbaToGrayFilter>("gpsrgb2gray"));

    // 1. Copy-on-Write with tee: Branch A untouched (100), Branch B thresholded (0)
    {
        GstElement* pipe = gst_pipeline_new("cow_tee");
        GstElement* src = gst_element_factory_make("appsrc", "src");
        GstElement* tee = gst_element_factory_make("tee", "tee");
        GstElement* q1 = gst_element_factory_make("queue", "q1");
        GstElement* q2 = gst_element_factory_make("queue", "q2");
        GstElement* filt = gst_element_factory_make("gpstestthresh", "filt");
        GstElement* sinkA = gst_element_factory_make("fakesink", "sinkA");
        GstElement* sinkB = gst_element_factory_make("fakesink", "sinkB");
        TEST_CHECK(pipe && src && tee && q1 && q2 && filt && sinkA && sinkB);

        auto caps = gps::elem::Caps::videoRaw({{"GRAY8"}, 16, 16, 30, 1, {}});
        g_object_set(src, "caps", caps.get(), "format", GST_FORMAT_TIME, nullptr);
        g_object_set(filt, "threshold", 128, nullptr);
        g_object_set(sinkA, "signal-handoffs", TRUE, nullptr);
        g_object_set(sinkB, "signal-handoffs", TRUE, nullptr);

        struct VerifyState {
            std::atomic<int> countA{0};
            std::atomic<int> countB{0};
            std::atomic<bool> okA{true};
            std::atomic<bool> okB{true};
        } state;

        g_signal_connect(sinkA, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer* buf, GstPad*, gpointer ud) {
            auto* st = static_cast<VerifyState*>(ud);
            GstMapInfo info{};
            if (gst_buffer_map(buf, &info, GST_MAP_READ)) {
                for (gsize i = 0; i < info.size; ++i) {
                    if (info.data[i] != 100) st->okA.store(false);
                }
                gst_buffer_unmap(buf, &info);
            }
            st->countA.fetch_add(1);
        }), &state);

        g_signal_connect(sinkB, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer* buf, GstPad*, gpointer ud) {
            auto* st = static_cast<VerifyState*>(ud);
            GstMapInfo info{};
            if (gst_buffer_map(buf, &info, GST_MAP_READ)) {
                for (gsize i = 0; i < info.size; ++i) {
                    if (info.data[i] != 0) st->okB.store(false);
                }
                gst_buffer_unmap(buf, &info);
            }
            st->countB.fetch_add(1);
        }), &state);

        gst_bin_add_many(GST_BIN(pipe), src, tee, q1, q2, filt, sinkA, sinkB, nullptr);
        TEST_CHECK(gst_element_link(src, tee));
        TEST_CHECK(gst_element_link_many(tee, q1, sinkA, nullptr));
        TEST_CHECK(gst_element_link_many(tee, q2, filt, sinkB, nullptr));

        TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

        for (int i = 0; i < 3; ++i) {
            auto buf = gps::elem::Buffer::allocate(16 * 16);
            {
                auto w = buf.write();
                std::memset(w.data(), 100, w.size());
            }
            buf.setPts(static_cast<GstClockTime>(i) * 33 * GST_MSECOND);
            GstFlowReturn ret = GST_FLOW_OK;
            g_signal_emit_by_name(src, "push-buffer", buf.get(), &ret);
            TEST_EQ(ret, GST_FLOW_OK);
        }
        GstFlowReturn eosRet = GST_FLOW_OK;
        g_signal_emit_by_name(src, "end-of-stream", &eosRet);

        TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_EOS);
        TEST_EQ(state.countA.load(), 3);
        TEST_EQ(state.countB.load(), 3);
        TEST_CHECK(state.okA.load());
        TEST_CHECK(state.okB.load());

        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }

    // 2. Out-of-place format conversion RGBA -> GRAY8
    {
        GstElement* pipe = gst_pipeline_new("oop_conv");
        GstElement* src = gst_element_factory_make("appsrc", "src");
        GstElement* conv = gst_element_factory_make("gpsrgb2gray", "conv");
        GstElement* sink = gst_element_factory_make("fakesink", "sink");
        TEST_CHECK(pipe && src && conv && sink);

        auto inCaps = gps::elem::Caps::videoRaw({{"RGBA"}, 8, 8, 30, 1, {}});
        g_object_set(src, "caps", inCaps.get(), "format", GST_FORMAT_TIME, nullptr);
        g_object_set(sink, "signal-handoffs", TRUE, nullptr);

        struct ConvState {
            std::atomic<int> count{0};
            std::atomic<bool> ok{true};
        } cstate;

        g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer* buf, GstPad*, gpointer ud) {
            auto* st = static_cast<ConvState*>(ud);
            GstMapInfo info{};
            if (gst_buffer_map(buf, &info, GST_MAP_READ)) {
                if (info.size != 64) st->ok.store(false);
                for (gsize i = 0; i < info.size; ++i) {
                    if (info.data[i] != 90) st->ok.store(false);
                }
                gst_buffer_unmap(buf, &info);
            }
            st->count.fetch_add(1);
        }), &cstate);

        gst_bin_add_many(GST_BIN(pipe), src, conv, sink, nullptr);
        TEST_CHECK(gst_element_link_many(src, conv, sink, nullptr));
        TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

        auto buf = gps::elem::Buffer::allocate(8 * 8 * 4);
        {
            auto w = buf.write();
            for (size_t i = 0; i < w.size(); i += 4) {
                w.data()[i + 0] = 90;
                w.data()[i + 1] = 90;
                w.data()[i + 2] = 90;
                w.data()[i + 3] = 255;
            }
        }
        buf.setPts(0);
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(src, "push-buffer", buf.get(), &ret);
        TEST_EQ(ret, GST_FLOW_OK);
        g_signal_emit_by_name(src, "end-of-stream", &ret);

        TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_EOS);
        TEST_EQ(cstate.count.load(), 1);
        TEST_CHECK(cstate.ok.load());

        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }

    return 0;
}
