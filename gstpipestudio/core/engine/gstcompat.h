// gstcompat.h – the two GStreamer calls that appeared in 1.20; older GStreamer (1.16 and up) gets the old equivalents.
#pragma once

#include <gst/gst.h>

namespace gps {

// Same call as gst_element_request_pad_simple, which 1.20 introduced as the new name.
inline GstPad* requestPadByName(GstElement* element, const char* name) {
#if GST_CHECK_VERSION(1, 20, 0)
    return gst_element_request_pad_simple(element, name);
#else
    return gst_element_get_request_pad(element, name);
#endif
}

// Reads a property value text. 1.20 knows the property's own type (for arrays and the like); older GStreamer reads
// by the value's type alone.
inline gboolean deserializeValue(GValue* value, const char* text, GParamSpec* spec) {
#if GST_CHECK_VERSION(1, 20, 0)
    return gst_value_deserialize_with_pspec(value, text, spec);
#else
    (void)spec;
    return gst_value_deserialize(value, text);
#endif
}

}  // namespace gps
