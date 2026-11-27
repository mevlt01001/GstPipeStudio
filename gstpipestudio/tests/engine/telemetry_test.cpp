#include "telemetry.h"
#include "graph.h"

#include <gst/gst.h>

#include <cstdio>

#define TEST_CHECK(cond)                                                               \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    gps::Graph g;
    const std::string srcName = g.addElement("videotestsrc");
    const std::string idName = g.addElement("identity");
    const std::string sinkName = g.addElement("fakesink");
    TEST_CHECK(g.linkExisting(srcName, "src", idName, "sink"));
    TEST_CHECK(g.linkExisting(idName, "src", sinkName, "sink"));

    GstElement* pipe = gst_pipeline_new("telpipe");
    GstElement* src = gst_element_factory_make("videotestsrc", srcName.c_str());
    GstElement* id = gst_element_factory_make("identity", idName.c_str());
    GstElement* sink = gst_element_factory_make("fakesink", sinkName.c_str());
    TEST_CHECK(pipe && src && id && sink);
    g_object_set(src, "num-buffers", 10, nullptr);
    gst_bin_add_many(GST_BIN(pipe), src, id, sink, nullptr);
    TEST_CHECK(gst_element_link_many(src, id, sink, nullptr));

    gps::TelemetrySampler sampler(g, pipe);
    TEST_CHECK(gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    GstBus* bus = gst_element_get_bus(pipe);
    GstMessage* msg = gst_bus_timed_pop_filtered(
        bus, 10 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    TEST_CHECK(msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    if (msg) gst_message_unref(msg);
    gst_object_unref(bus);

    // Also simulate a QoS message on "id" to verify atomic + per-node QoS delta accounting.
    GstMessage* qos = gst_message_new_qos(GST_OBJECT(id), TRUE, 0, 0, 0, 0);
    gst_message_set_qos_stats(qos, GST_FORMAT_BUFFERS, 8, 2);
    sampler.onQos(qos);
    gst_message_unref(qos);

    const gps::TelemetrySample s = sampler.sample(100.0);
    TEST_CHECK(s.elements.size() == 3);
    TEST_CHECK(s.elements[0].outputBuffers == 10);
    TEST_CHECK(s.elements[1].inputBuffers == 10);
    TEST_CHECK(s.elements[1].outputBuffers == 10);
    TEST_CHECK(s.elements[1].qosProcessed == 8);
    TEST_CHECK(s.elements[1].qosDropped == 2);
    TEST_CHECK(s.elements[1].dropRatePercent > 19.0 && s.elements[1].dropRatePercent < 21.0);
    TEST_CHECK(s.elements[2].inputBuffers == 10);

    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    return 0;
}
