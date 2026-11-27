#include "gps_elem/element.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <stdexcept>

namespace {

class TestGeneralNode : public gps::elem::Element {
public:
    explicit TestGeneralNode(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Test General 2x2", "Filter/General", "2->2 + Mutability::Null + exception barrier", "GstPipeStudio");
        b.addSinkPort("sink_0", gps::elem::Caps::any(), gps::elem::Presence::Always, gps::elem::InputMode::Direct);
        b.addSinkPort("sink_1", gps::elem::Caps::any(), gps::elem::Presence::Always, gps::elem::InputMode::Direct);
        b.addSrcPort("src_0", gps::elem::Caps::any(), gps::elem::Presence::Always);
        b.addSrcPort("src_1", gps::elem::Caps::any(), gps::elem::Presence::Always);

        b.properties.prop("null-only-id", &TestGeneralNode::m_nullOnlyId, 1, 0, 1000)
            .mutability(gps::elem::Mutability::Null);
        b.properties.prop("throw-on-frame", &TestGeneralNode::m_throwOnFrame, false)
            .mutability(gps::elem::Mutability::Playing);
    }

    bool open() override {
        auto* s0 = sinkPort("sink_0");
        auto* s1 = sinkPort("sink_1");
        auto* o0 = srcPort("src_0");
        auto* o1 = srcPort("src_1");
        if (!s0 || !s1 || !o0 || !o1) return false;
        linkInternal(*s0, *o0);
        linkInternal(*s1, *o1);

        s0->setDirectCallback([this, o0](gps::elem::Buffer buf) -> GstFlowReturn {
            if (m_throwOnFrame) {
                throw std::runtime_error("intentional test exception");
            }
            return o0->push(std::move(buf));
        });
        s1->setDirectCallback([o1](gps::elem::Buffer buf) -> GstFlowReturn {
            return o1->push(std::move(buf));
        });
        return true;
    }

private:
    int32_t m_nullOnlyId = 1;
    bool m_throwOnFrame = false;
};

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<TestGeneralNode>("gpstestgeneral"));

    GstElement* pipe = gst_pipeline_new("general_test");
    GstElement* src0 = gst_element_factory_make("appsrc", "src0");
    GstElement* src1 = gst_element_factory_make("appsrc", "src1");
    GstElement* node = gst_element_factory_make("gpstestgeneral", "node");
    GstElement* sink0 = gst_element_factory_make("fakesink", "sink0");
    GstElement* sink1 = gst_element_factory_make("fakesink", "sink1");
    TEST_CHECK(pipe && src0 && src1 && node && sink0 && sink1);

    // Mutability::Null allowed in NULL state
    g_object_set(node, "null-only-id", 42, nullptr);
    gint val = 0;
    g_object_get(node, "null-only-id", &val, nullptr);
    TEST_EQ(val, 42);

    gst_bin_add_many(GST_BIN(pipe), src0, src1, node, sink0, sink1, nullptr);
    TEST_CHECK(gst_element_link_pads(src0, "src", node, "sink_0"));
    TEST_CHECK(gst_element_link_pads(src1, "src", node, "sink_1"));
    TEST_CHECK(gst_element_link_pads(node, "src_0", sink0, "sink"));
    TEST_CHECK(gst_element_link_pads(node, "src_1", sink1, "sink"));

    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    // Mutability::Null guard in PLAYING: setting must be rejected!
    g_object_set(node, "null-only-id", 99, nullptr);
    g_object_get(node, "null-only-id", &val, nullptr);
    TEST_EQ(val, 42);

    // C++ exception barrier: throw std::runtime_error -> GST_ELEMENT_ERROR on bus + GST_FLOW_ERROR without crash
    g_object_set(node, "throw-on-frame", TRUE, nullptr);
    auto buf = gps::elem::Buffer::allocate(8);
    GstFlowReturn fret = GST_FLOW_OK;
    g_signal_emit_by_name(src0, "push-buffer", buf.get(), &fret);
    (void)fret;

    TEST_EQ(waitForBusTerminal(pipe), GST_MESSAGE_ERROR);

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    return 0;
}
