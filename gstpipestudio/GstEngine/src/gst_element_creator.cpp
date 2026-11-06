// gst_element_creator.cpp – implementation of GstElementCreator.
#include "gst_element_creator.h"

#include "gstcompat.h"
#include "inspector.h"

namespace gps {
namespace {

std::string trim(const std::string& text) {
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return text.substr(a, text.find_last_not_of(" \t\r\n") - a + 1);
}

}  // namespace

std::string GstElementCreator::formatPropertyError(const std::string& label, const std::string& text,
                                                   const std::string& reason) {
    return label + ": '" + text + "' is not valid, " + reason;
}

bool GstElementCreator::setProperty(GObject* object, const std::string& property, const std::string& text,
                                    std::string* reason) {
    auto refuse = [&](const std::string& why) {
        if (reason) *reason = why;
        return false;
    };
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(object), property.c_str());
    if (!spec) return refuse("there is no such property");
    if (!(spec->flags & G_PARAM_WRITABLE)) return refuse("the property is read-only");
    if (spec->flags & G_PARAM_CONSTRUCT_ONLY) return refuse("it can only be set when the element is created");
    if (spec->owner_type == GST_TYPE_OBJECT) return refuse("it is a GstObject property that a plan cannot change");

    GType kind = G_TYPE_FUNDAMENTAL(spec->value_type);
    bool unsignedNumber = kind == G_TYPE_UINT || kind == G_TYPE_UINT64 || kind == G_TYPE_ULONG || kind == G_TYPE_UCHAR;
    bool needsValue = unsignedNumber || kind == G_TYPE_INT || kind == G_TYPE_INT64 || kind == G_TYPE_LONG ||
                      kind == G_TYPE_CHAR || kind == G_TYPE_FLOAT || kind == G_TYPE_DOUBLE || kind == G_TYPE_ENUM ||
                      kind == G_TYPE_BOOLEAN;
    std::string trimmed = trim(text);
    if (needsValue && trimmed.empty()) return refuse("an empty text is not a value");
    if (unsignedNumber && trimmed[0] == '-') return refuse("a negative number is not allowed, expected an unsigned number");

    GValue value = G_VALUE_INIT;
    g_value_init(&value, spec->value_type);
    if (!deserializeValue(&value, text.c_str(), spec)) {
        g_value_unset(&value);
        return refuse(std::string("it cannot be read as ") + g_type_name(spec->value_type));
    }
    if (g_param_value_validate(spec, &value)) {
        g_value_unset(&value);
        PropertyInfo info = describeProperty(spec);
        return refuse(info.min.empty() ? "it is outside the allowed range" : "allowed range is " + info.min + " to " + info.max);
    }
    g_value_unset(&value);
    gst_util_set_object_arg(object, property.c_str(), text.c_str());
    return true;
}

GstElement* GstElementCreator::createElement(const Element& spec, std::string* error, const LogFn& log) {
    GstElementFactory* known = gst_element_factory_find(spec.factory.c_str());
    if (known) gst_object_unref(known);
    if (!known) {
        if (error) *error = spec.name + ": element '" + spec.factory + "' was not found, its plugin may not be installed";
        return nullptr;
    }
    GstElement* element = gst_element_factory_make(spec.factory.c_str(), spec.name.c_str());
    if (!element) {
        if (error) *error = spec.name + ": unable to create element '" + spec.factory + "'";
        return nullptr;
    }
    if (log) log("element: " + spec.name + " (" + spec.factory + ")");
    for (const auto& kv : spec.props) {
        std::string reason;
        if (!setProperty(G_OBJECT(element), kv.first, kv.second, &reason)) {
            if (error) *error = formatPropertyError(spec.name + "." + kv.first, kv.second, reason);
            gst_object_unref(element);
            return nullptr;
        }
        if (log) log("property: " + spec.name + "." + kv.first + " = " + kv.second);
    }
    return element;
}

GstPad* GstElementCreator::requestPad(GstElement* element, const std::string& templateName, const LogFn& log) {
    GstPad* pad = requestPadByName(element, templateName.c_str());
    if (pad) return pad;
    size_t slot = templateName.find("%u");
    if (slot == std::string::npos) slot = templateName.find("%d");
    if (slot == std::string::npos) return nullptr;
    for (int n = 0; n < 64; ++n) {
        std::string numbered = templateName;
        numbered.replace(slot, 2, std::to_string(n));
        if (GstPad* existing = gst_element_get_static_pad(element, numbered.c_str())) {
            gst_object_unref(existing);
            continue;
        }
        pad = requestPadByName(element, numbered.c_str());
        if (pad) {
            if (log) log("request: '" + templateName + "' was not accepted, asked for '" + numbered + "'");
            return pad;
        }
    }
    return nullptr;
}

FactoryTraits GstElementCreator::queryFactoryTraits(const std::string& factory) {
    FactoryTraits traits;
    GstElementFactory* f = gst_element_factory_find(factory.c_str());
    if (!f) return traits;
    bool hasSink = false;
    for (const GList* t = gst_element_factory_get_static_pad_templates(f); t; t = t->next) {
        const GstStaticPadTemplate* tmpl = static_cast<const GstStaticPadTemplate*>(t->data);
        if (tmpl->direction == GST_PAD_SINK) hasSink = true;
    }
    traits.isSource = !hasSink;
    gst_object_unref(f);
    return traits;
}

}  // namespace gps

