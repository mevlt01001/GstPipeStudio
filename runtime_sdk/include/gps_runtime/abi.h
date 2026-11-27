#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPS_PIPELINE_ABI_VERSION 1u

typedef enum GpsRuntimeParamType {
    GPS_PARAM_STRING = 0,
    GPS_PARAM_BOOL = 1,
    GPS_PARAM_INT64 = 2,
    GPS_PARAM_DOUBLE = 3,
} GpsRuntimeParamType;

typedef enum GpsRuntimePortDirection {
    GPS_PORT_SINK = 0,
    GPS_PORT_SRC = 1,
} GpsRuntimePortDirection;

typedef struct GpsRuntimeParamDesc {
    const char* name;
    const char* element;
    const char* property;
    GpsRuntimeParamType type;
    const char* default_value;
} GpsRuntimeParamDesc;

typedef struct GpsRuntimePortDesc {
    const char* name;
    GpsRuntimePortDirection direction;
    const char* caps;
} GpsRuntimePortDesc;

typedef struct GpsRuntimeElementMetric {
    const char* element;
    double age_ms;
    double queue_ms;
    double processing_ms;
    double drop_rate_percent;
    uint64_t input_buffers;
    uint64_t output_buffers;
} GpsRuntimeElementMetric;

typedef struct GpsRuntimeMetrics {
    double elapsed_ms;
    const GpsRuntimeElementMetric* elements;
    size_t element_count;
} GpsRuntimeMetrics;

typedef struct GpsRuntimeFrame {
    const uint8_t* data;
    size_t size;
    uint64_t pts_ns;
    uint64_t duration_ns;
    const char* caps;
} GpsRuntimeFrame;

typedef void (*GpsRuntimeFrameCallback)(const char* port_name, const GpsRuntimeFrame* frame, void* user_data);
typedef void (*GpsRuntimeLogCallback)(int level, const char* message, void* user_data);

typedef struct GpsPipelineHandle GpsPipelineHandle;

typedef struct GpsPipelineModuleV1 {
    uint32_t abi_version;
    const char* pipeline_name;
    const char* pipeline_version;
    const GpsRuntimeParamDesc* params;
    size_t param_count;
    const GpsRuntimePortDesc* ports;
    size_t port_count;

    GpsPipelineHandle* (*create)(char* err_buf, size_t err_cap);
    void (*destroy)(GpsPipelineHandle* handle);

    int (*start)(GpsPipelineHandle* handle, char* err_buf, size_t err_cap);
    int (*pause)(GpsPipelineHandle* handle, char* err_buf, size_t err_cap);
    int (*stop)(GpsPipelineHandle* handle);
    int (*send_eos)(GpsPipelineHandle* handle);
    int (*wait_for_eos)(GpsPipelineHandle* handle, int64_t timeout_ms, char* err_buf, size_t err_cap);

    int (*set_param)(GpsPipelineHandle* handle, const char* name, const char* value, char* err_buf, size_t err_cap);
    int (*get_param)(GpsPipelineHandle* handle, const char* name, char* out_buf, size_t out_cap);

    int (*push_frame)(GpsPipelineHandle* handle, const char* port_name, const uint8_t* data, size_t size, uint64_t pts_ns,
                      const char* caps, char* err_buf, size_t err_cap);
    void (*set_frame_callback)(GpsPipelineHandle* handle, const char* port_name, GpsRuntimeFrameCallback cb, void* user_data);
    void (*set_log_callback)(GpsPipelineHandle* handle, GpsRuntimeLogCallback cb, void* user_data);
    int (*get_metrics)(GpsPipelineHandle* handle, GpsRuntimeMetrics* out_metrics);
} GpsPipelineModuleV1;

typedef const GpsPipelineModuleV1* (*GpsPipelineGetModuleV1Fn)(void);

#define GPS_PIPELINE_MODULE_SYMBOL "gps_pipeline_get_module_v1"

#ifdef __cplusplus
}
#endif
