// nativepad.h – how a native GstPad is named in Graph terms (Connection in graph.h). Builder and importer read pads only here.
#pragma once

#include <string>

#include <gst/gst.h>

namespace gps {

// always | sometimes | request
const char* presenceName(GstPadPresence presence);

struct NativePad {
    explicit NativePad(GstPad* pad);

    std::string name, templateName;
    GstPadPresence presence = GST_PAD_SOMETIMES;  // a pad without template counts as a Sometimes pad named after itself

    // Src: Always = its name; Sometimes and Request = "<template>#<k>", where k is ordinal, counted per template by the caller.
    std::string srcIdentity(int& ordinal) const;
    // Sink: Request = the template name (every link asks for its own pad); otherwise its name.
    std::string sinkIdentity() const;
};

}  // namespace gps
