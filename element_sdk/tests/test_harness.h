#pragma once

#include <gst/gst.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#define TEST_CHECK(cond)                                                              \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            std::abort();                                                             \
        }                                                                             \
    } while (0)

#define TEST_EQ(a, b)                                                                 \
    do {                                                                              \
        auto va = (a);                                                                \
        auto vb = (b);                                                                \
        if (!(va == vb)) {                                                            \
            std::fprintf(stderr, "FAILED %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
            std::abort();                                                             \
        }                                                                             \
    } while (0)

inline void initGst(int* argc, char*** argv) {
    gst_init(argc, argv);
}

inline GstMessageType waitForBusTerminal(GstElement* pipeline, GstClockTime timeoutNs = 5 * GST_SECOND) {
    GstBus* bus = gst_element_get_bus(pipeline);
    GstMessage* msg = gst_bus_timed_pop_filtered(
        bus, timeoutNs, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    GstMessageType type = GST_MESSAGE_UNKNOWN;
    if (msg) {
        type = GST_MESSAGE_TYPE(msg);
        if (type == GST_MESSAGE_ERROR) {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            std::fprintf(stderr, "Bus ERROR: %s (%s)\n", err ? err->message : "", dbg ? dbg : "");
            if (err) g_error_free(err);
            g_free(dbg);
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
    return type;
}
