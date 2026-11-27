#pragma once

#include "gps_elem/element.h"

#include <memory>
#include <string>

namespace gps::elem {

template <typename T>
GType ensureElementGType(const std::string& typeName) {
    ElementClassBuilder builder;
    T::describe(builder);
    return Element::registerType(
        typeName,
        std::move(builder),
        [](GstElement* raw) -> std::unique_ptr<Element> {
            return std::make_unique<T>(raw);
        });
}

template <typename T>
bool registerElement(GstPlugin* plugin, const std::string& name, GstRank rank = GST_RANK_NONE) {
    GType type = ensureElementGType<T>(name + "_gps_type");
    return gst_element_register(plugin, name.c_str(), rank, type) != FALSE;
}

template <typename T>
bool registerElementStatic(const std::string& name, GstRank rank = GST_RANK_NONE) {
    return registerElement<T>(nullptr, name, rank);
}

}  // namespace gps::elem

#ifndef PACKAGE
#define PACKAGE "gps_element_sdk"
#endif

#define GPS_ELEM_DEFINE_PLUGIN(pluginName, description, version, license, package, origin, initBlock) \
    static gboolean gps_elem_plugin_init(GstPlugin* plugin) {                                         \
        try {                                                                                         \
            initBlock                                                                                 \
        } catch (...) {                                                                               \
            return FALSE;                                                                             \
        }                                                                                             \
    }                                                                                                 \
    GST_PLUGIN_DEFINE(                                                                                \
        GST_VERSION_MAJOR,                                                                            \
        GST_VERSION_MINOR,                                                                            \
        pluginName,                                                                                   \
        description,                                                                                  \
        gps_elem_plugin_init,                                                                         \
        version,                                                                                      \
        license,                                                                                      \
        package,                                                                                      \
        origin)
