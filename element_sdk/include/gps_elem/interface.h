#pragma once

#include <gst/gst.h>

#include <memory>
#include <string>
#include <vector>

namespace gps::elem {

struct InterfaceSpec {
    GType interfaceType = G_TYPE_INVALID;
    GInterfaceInitFunc interfaceInit = nullptr;
    gpointer interfaceData = nullptr;
};

struct UriHandlerMeta {
    GstURIType uriType = GST_URI_UNKNOWN;
    std::vector<std::string> protocols;
    std::vector<const gchar*> protocolPtrs;

    void finalizeProtocols() {
        protocolPtrs.clear();
        protocolPtrs.reserve(protocols.size() + 1);
        for (const auto& p : protocols) {
            protocolPtrs.push_back(p.c_str());
        }
        protocolPtrs.push_back(nullptr);
    }
};

}  // namespace gps::elem
