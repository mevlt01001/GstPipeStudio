// nativepad.cpp – see nativepad.h.
#include "nativepad.h"

#include "graph.h"

namespace gps {

const char* presenceName(GstPadPresence presence) {
    switch (presence) {
        case GST_PAD_ALWAYS: return "always";
        case GST_PAD_SOMETIMES: return "sometimes";
        case GST_PAD_REQUEST: return "request";
    }
    return "sometimes";
}

NativePad::NativePad(GstPad* pad) : name(GST_PAD_NAME(pad)), templateName(name) {
    if (GstPadTemplate* tmpl = gst_pad_get_pad_template(pad)) {
        templateName = GST_PAD_TEMPLATE_NAME_TEMPLATE(tmpl);
        presence = GST_PAD_TEMPLATE_PRESENCE(tmpl);
        gst_object_unref(tmpl);
    }
}

std::string NativePad::srcIdentity(int& ordinal) const {
    return presence == GST_PAD_ALWAYS ? name : padIdentity(templateName, ordinal++);
}

std::string NativePad::sinkIdentity() const {
    return presence == GST_PAD_REQUEST ? templateName : name;
}

}  // namespace gps
