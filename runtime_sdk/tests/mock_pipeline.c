#include "gps_runtime/abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GpsPipelineHandle {
    int running;
    int eos_sent;
    char param_val[64];
    GpsRuntimeFrameCallback frame_cb;
    void* frame_ud;
    GpsRuntimeLogCallback log_cb;
    void* log_ud;
};

static const GpsRuntimeParamDesc kParams[] = {
    {"gain", "amp_0", "volume", GPS_PARAM_DOUBLE, "1.0"},
};

static const GpsRuntimePortDesc kPorts[] = {
    {"sink_in", GPS_PORT_SINK, "application/x-raw"},
    {"src_out", GPS_PORT_SRC, "application/x-raw"},
};

static GpsPipelineHandle* m_create(char* err, size_t cap) {
    (void)err;
    (void)cap;
    GpsPipelineHandle* h = (GpsPipelineHandle*)calloc(1, sizeof(GpsPipelineHandle));
    snprintf(h->param_val, sizeof(h->param_val), "1.0");
    return h;
}

static void m_destroy(GpsPipelineHandle* h) { free(h); }

static int m_start(GpsPipelineHandle* h, char* err, size_t cap) {
    (void)err;
    (void)cap;
    h->running = 1;
    if (h->log_cb) h->log_cb(1, "mock started", h->log_ud);
    return 1;
}

static int m_pause(GpsPipelineHandle* h, char* err, size_t cap) {
    (void)err;
    (void)cap;
    h->running = 0;
    return 1;
}

static int m_stop(GpsPipelineHandle* h) {
    h->running = 0;
    return 1;
}

static int m_send_eos(GpsPipelineHandle* h) {
    h->eos_sent = 1;
    return 1;
}

static int m_wait_eos(GpsPipelineHandle* h, int64_t timeout_ms, char* err, size_t cap) {
    (void)timeout_ms;
    (void)err;
    (void)cap;
    return h->eos_sent ? 1 : 0;
}

static int m_set_param(GpsPipelineHandle* h, const char* name, const char* value, char* err, size_t cap) {
    if (strcmp(name, "gain") != 0) {
        snprintf(err, cap, "unknown parameter");
        return 0;
    }
    snprintf(h->param_val, sizeof(h->param_val), "%s", value);
    return 1;
}

static int m_get_param(GpsPipelineHandle* h, const char* name, char* out, size_t cap) {
    if (strcmp(name, "gain") != 0) return 0;
    snprintf(out, cap, "%s", h->param_val);
    return 1;
}

static int m_push_frame(GpsPipelineHandle* h, const char* port, const uint8_t* data, size_t size, uint64_t pts,
                        const char* caps, char* err, size_t cap) {
    (void)port;
    (void)err;
    (void)cap;
    if (h->frame_cb) {
        GpsRuntimeFrame f = {data, size, pts, 0, caps};
        h->frame_cb("src_out", &f, h->frame_ud);
    }
    return 1;
}

static void m_set_frame_cb(GpsPipelineHandle* h, const char* port, GpsRuntimeFrameCallback cb, void* ud) {
    (void)port;
    h->frame_cb = cb;
    h->frame_ud = ud;
}

static void m_set_log_cb(GpsPipelineHandle* h, GpsRuntimeLogCallback cb, void* ud) {
    h->log_cb = cb;
    h->log_ud = ud;
}

static int m_get_metrics(GpsPipelineHandle* h, GpsRuntimeMetrics* out) {
    (void)h;
    static const GpsRuntimeElementMetric kElems[] = {{"amp_0", 1.5, 0.0, 2.5, 0.0, 4, 4}};
    out->elapsed_ms = 42.0;
    out->elements = kElems;
    out->element_count = 1;
    return 1;
}

static const GpsPipelineModuleV1 kModule = {
    GPS_PIPELINE_ABI_VERSION,
    "mock_pipeline",
    "0.1.0",
    kParams,
    1,
    kPorts,
    2,
    m_create,
    m_destroy,
    m_start,
    m_pause,
    m_stop,
    m_send_eos,
    m_wait_eos,
    m_set_param,
    m_get_param,
    m_push_frame,
    m_set_frame_cb,
    m_set_log_cb,
    m_get_metrics,
};

const GpsPipelineModuleV1* gps_pipeline_get_module_v1(void) { return &kModule; }
