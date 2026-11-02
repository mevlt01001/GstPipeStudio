// gstgpsconf.cpp – the GStreamer plugin of SPEC 6.
// Every *.conf in GPS_PIPELINE_PATH becomes an element: a GstBin subclass that builds the Plan with the same
// Builder as gps-run. GstBin brings GstChildProxy, so "demo src::num-buffers=3" reaches every inner property.
// Exposed parameters become properties (type, range and default cloned from the target property, else string).
// Free src pads of an incomplete Plan become ghost pads of the bin.
#include <gst/gst.h>

#include <algorithm>
#include <string>
#include <vector>

#include "builder.h"
#include "plan.h"

GST_DEBUG_CATEGORY_STATIC(gpsconf_debug);
#define GST_CAT_DEFAULT gpsconf_debug

namespace {

struct Slot {
    guint id;
    gps::ExposedParam param;
};

struct ConfClass {
    GstBinClass parent;
    gps::Plan* plan;
    std::vector<Slot>* slots;
    std::string* loadError;
};

struct Conf {
    GstBin parent;
    gps::Builder* builder;
    std::string* error;  // why the Plan could not be built; the element then fails at NULL -> READY
};

gpointer parentClass = nullptr;

// The property of the target element, or null. Needs the target's plugin loaded.
GParamSpec* targetSpec(const gps::Plan& plan, const gps::ExposedParam& param) {
    const gps::PlanElement* element = plan.find(param.element);
    if (!element) return nullptr;
    GstElementFactory* factory = gst_element_factory_find(element->factory.c_str());
    if (!factory) return nullptr;
    GstPluginFeature* loaded = gst_plugin_feature_load(GST_PLUGIN_FEATURE(factory));
    gst_object_unref(factory);
    if (!loaded) return nullptr;
    gpointer klass = g_type_class_ref(gst_element_factory_get_element_type(GST_ELEMENT_FACTORY(loaded)));
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_CLASS(klass), param.property.c_str());
    g_type_class_unref(klass);  // the class stays alive for the process, so the spec stays valid
    gst_object_unref(loaded);
    return spec;
}

// Same type, range and default as the target, by the type of the value. No element is known by name.
GParamSpec* cloneSpec(const std::string& name, const std::string& blurbPrefix, GParamSpec* t) {
    const gchar* nick = t ? g_param_spec_get_nick(t) : name.c_str();
    std::string blurb = blurbPrefix + (t && g_param_spec_get_blurb(t) ? g_param_spec_get_blurb(t) : "");
    GParamFlags flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    if (!t || !(t->flags & G_PARAM_WRITABLE)) return g_param_spec_string(name.c_str(), name.c_str(), blurb.c_str(), nullptr, flags);
    flags = static_cast<GParamFlags>(flags | (t->flags & (GST_PARAM_MUTABLE_READY | GST_PARAM_MUTABLE_PAUSED | GST_PARAM_MUTABLE_PLAYING)));
    const char *n = name.c_str(), *b = blurb.c_str();
    switch (G_TYPE_FUNDAMENTAL(t->value_type)) {
    case G_TYPE_BOOLEAN: return g_param_spec_boolean(n, nick, b, G_PARAM_SPEC_BOOLEAN(t)->default_value, flags);
    case G_TYPE_INT: { auto* s = G_PARAM_SPEC_INT(t); return g_param_spec_int(n, nick, b, s->minimum, s->maximum, s->default_value, flags); }
    case G_TYPE_UINT: { auto* s = G_PARAM_SPEC_UINT(t); return g_param_spec_uint(n, nick, b, s->minimum, s->maximum, s->default_value, flags); }
    case G_TYPE_INT64: { auto* s = G_PARAM_SPEC_INT64(t); return g_param_spec_int64(n, nick, b, s->minimum, s->maximum, s->default_value, flags); }
    case G_TYPE_UINT64: { auto* s = G_PARAM_SPEC_UINT64(t); return g_param_spec_uint64(n, nick, b, s->minimum, s->maximum, s->default_value, flags); }
    case G_TYPE_FLOAT: { auto* s = G_PARAM_SPEC_FLOAT(t); return g_param_spec_float(n, nick, b, s->minimum, s->maximum, s->default_value, flags); }
    case G_TYPE_DOUBLE: { auto* s = G_PARAM_SPEC_DOUBLE(t); return g_param_spec_double(n, nick, b, s->minimum, s->maximum, s->default_value, flags); }
    case G_TYPE_STRING: return g_param_spec_string(n, nick, b, G_PARAM_SPEC_STRING(t)->default_value, flags);
    case G_TYPE_ENUM: return g_param_spec_enum(n, nick, b, t->value_type, G_PARAM_SPEC_ENUM(t)->default_value, flags);
    case G_TYPE_FLAGS: return g_param_spec_flags(n, nick, b, t->value_type, G_PARAM_SPEC_FLAGS(t)->default_value, flags);
    default: return g_param_spec_string(n, nick, b, nullptr, flags);  // caps, fractions, objects ...: as text
    }
}

const Slot* slotOf(guint id, ConfClass* klass) {
    for (const Slot& s : *klass->slots) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

void setProp(GObject* object, guint id, const GValue* value, GParamSpec*) {
    Conf* self = reinterpret_cast<Conf*>(object);
    const Slot* slot = slotOf(id, reinterpret_cast<ConfClass*>(G_OBJECT_GET_CLASS(object)));
    if (!slot) return;
    GstElement* child = gst_bin_get_by_name(GST_BIN(self), slot->param.element.c_str());
    if (!child) {
        GST_WARNING_OBJECT(self, "parameter %s: no element %s inside", slot->param.name.c_str(), slot->param.element.c_str());
        return;
    }
    gchar* text = G_VALUE_HOLDS_STRING(value) ? g_value_dup_string(value) : gst_value_serialize(value);
    std::string reason;
    // Same path as gps-run: checked against the target's own spec, then gst_util_set_object_arg.
    if (!gps::setProperty(G_OBJECT(child), slot->param.property, text ? text : "", &reason)) {
        GST_WARNING_OBJECT(self, "%s -> %s.%s: '%s' gecersiz (%s)", slot->param.name.c_str(), slot->param.element.c_str(),
                           slot->param.property.c_str(), text ? text : "", reason.c_str());
    }
    g_free(text);
    gst_object_unref(child);
}

void getProp(GObject* object, guint id, GValue* value, GParamSpec*) {
    Conf* self = reinterpret_cast<Conf*>(object);
    const Slot* slot = slotOf(id, reinterpret_cast<ConfClass*>(G_OBJECT_GET_CLASS(object)));
    if (!slot) return;
    GstElement* child = gst_bin_get_by_name(GST_BIN(self), slot->param.element.c_str());
    GParamSpec* target = child ? g_object_class_find_property(G_OBJECT_GET_CLASS(child), slot->param.property.c_str()) : nullptr;
    if (target && (target->flags & G_PARAM_READABLE)) {
        GValue current = G_VALUE_INIT;
        g_value_init(&current, target->value_type);
        g_object_get_property(G_OBJECT(child), slot->param.property.c_str(), &current);
        if (G_VALUE_TYPE(&current) == G_VALUE_TYPE(value)) {
            g_value_copy(&current, value);
        } else {
            gchar* text = gst_value_serialize(&current);  // string parameter standing in for another type
            g_value_take_string(value, text);
        }
        g_value_unset(&current);
    }
    if (child) gst_object_unref(child);
}

GstStateChangeReturn changeState(GstElement* element, GstStateChange transition) {
    Conf* self = reinterpret_cast<Conf*>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && !self->error->empty()) {
        GST_ELEMENT_ERROR(element, CORE, FAILED, ("%s", self->error->c_str()), (nullptr));
        return GST_STATE_CHANGE_FAILURE;
    }
    return GST_ELEMENT_CLASS(parentClass)->change_state(element, transition);
}

void disposeConf(GObject* object) {
    Conf* self = reinterpret_cast<Conf*>(object);
    delete self->builder;  // disconnects its signal handlers while the children still exist
    self->builder = nullptr;
    G_OBJECT_CLASS(parentClass)->dispose(object);
}

void finalizeConf(GObject* object) {
    Conf* self = reinterpret_cast<Conf*>(object);
    delete self->error;
    self->error = nullptr;
    G_OBJECT_CLASS(parentClass)->finalize(object);
}

void classInit(gpointer g_class, gpointer classData) {
    ConfClass* klass = static_cast<ConfClass*>(g_class);
    parentClass = g_type_class_peek_parent(g_class);
    klass->plan = new gps::Plan;
    klass->slots = new std::vector<Slot>;
    klass->loadError = new std::string;
    if (!gps::Plan::load(static_cast<const char*>(classData), klass->plan, klass->loadError)) {
        GST_WARNING("%s: %s", static_cast<const char*>(classData), klass->loadError->c_str());
    }

    GObjectClass* object = G_OBJECT_CLASS(g_class);
    object->set_property = setProp;
    object->get_property = getProp;
    object->dispose = disposeConf;
    object->finalize = finalizeConf;
    GST_ELEMENT_CLASS(g_class)->change_state = changeState;
    gst_element_class_set_metadata(GST_ELEMENT_CLASS(g_class), klass->plan->name.c_str(), "Generic/Bin",
                                   (std::string("GstPipeStudio pipeline from ") + static_cast<const char*>(classData)).c_str(),
                                   "GstPipeStudio");

    guint id = 1;
    for (const gps::ExposedParam& param : klass->plan->exposed) {
        if (!g_param_spec_is_valid_name(param.name.c_str()) || g_object_class_find_property(object, param.name.c_str())) {
            GST_WARNING("parameter '%s' skipped: not a property name, or the bin has it already", param.name.c_str());
            continue;
        }
        GParamSpec* target = targetSpec(*klass->plan, param);
        std::string prefix = param.element + "." + param.property + ": ";
        g_object_class_install_property(object, id, cloneSpec(param.name, prefix, target));
        klass->slots->push_back({id, param});
        ++id;
    }
}

void instanceInit(GTypeInstance* instance, gpointer g_class) {
    Conf* self = reinterpret_cast<Conf*>(instance);
    ConfClass* klass = static_cast<ConfClass*>(g_class);
    self->error = new std::string;
    self->builder = nullptr;
    if (!klass->loadError->empty()) {
        *self->error = "conf okunamadi: " + *klass->loadError;
        return;
    }
    gps::BuildOptions options;
    options.ghostPads = true;  // optional in SPEC 6; it is a few lines in the Builder
    self->builder = new gps::Builder(*klass->plan, GST_BIN(self), options, [self](const std::string& line) {
        GST_DEBUG_OBJECT(self, "%s", line.c_str());
    });
    if (!self->builder->build()) *self->error = self->builder->failure().message;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

gboolean pluginInit(GstPlugin* plugin) {
    GST_DEBUG_CATEGORY_INIT(gpsconf_debug, "gpsconf", 0, "GstPipeStudio conf elements");
    // A conf added or changed later makes GStreamer rescan this plugin instead of using its cached list.
    gst_plugin_add_dependency_simple(plugin, "GPS_PIPELINE_PATH", nullptr, ".conf", GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_SUFFIX);

    const char* env = g_getenv("GPS_PIPELINE_PATH");
    if (!env) return TRUE;  // no folder, no elements
    gchar** dirs = g_strsplit(env, G_SEARCHPATH_SEPARATOR_S, -1);
    std::vector<std::pair<std::string, std::string>> found;  // element name, file
    for (gchar** dir = dirs; *dir; ++dir) {
        GDir* handle = g_dir_open(*dir, 0, nullptr);
        if (!handle) continue;
        while (const gchar* file = g_dir_read_name(handle)) {
            std::string name = file;
            if (!endsWith(name, ".conf") || name.size() == 5) continue;
            found.emplace_back(name.substr(0, name.size() - 5), std::string(*dir) + G_DIR_SEPARATOR_S + name);
        }
        g_dir_close(handle);
    }
    g_strfreev(dirs);
    std::sort(found.begin(), found.end());

    for (const auto& entry : found) {
        std::string type = "GstGpsConf_" + entry.first;
        for (char& c : type) {
            if (!g_ascii_isalnum(c) && c != '_' && c != '-') c = '_';
        }
        gps::Plan plan;
        std::string error;
        if (!gps::Plan::load(entry.second, &plan, &error)) {
            GST_WARNING("%s skipped: %s", entry.second.c_str(), error.c_str());
            continue;
        }
        if (g_type_from_name(type.c_str())) {
            GST_WARNING("%s skipped: element '%s' is already defined by an earlier folder", entry.second.c_str(), entry.first.c_str());
            continue;
        }
        GTypeInfo info = {};
        info.class_size = sizeof(ConfClass);
        info.class_init = classInit;
        info.class_data = g_strdup(entry.second.c_str());  // lives as long as the type
        info.instance_size = sizeof(Conf);
        info.instance_init = instanceInit;
        GType gtype = g_type_register_static(GST_TYPE_BIN, type.c_str(), &info, static_cast<GTypeFlags>(0));
        if (!gst_element_register(plugin, entry.first.c_str(), GST_RANK_NONE, gtype)) {
            GST_WARNING("%s: could not register element '%s'", entry.second.c_str(), entry.first.c_str());
        }
    }
    return TRUE;
}

}  // namespace

#define PACKAGE "gstpipestudio"  // GST_PLUGIN_DEFINE wants it
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gpsconf, "GstPipeStudio .conf files as elements", pluginInit, "0.1",
                  "LGPL", "gstpipestudio", "https://invalid.example/")
