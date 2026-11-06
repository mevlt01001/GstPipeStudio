// inspector.cpp – see inspector.h.
#include "inspector.h"
#include "gstcompat.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>

namespace gps {
namespace {

std::string orEmpty(const gchar* text) { return text ? text : ""; }

// Shortest text that reads back as the same number ("0.1", not "0.10000000149011612").
std::string shortest(double value, bool isFloat) {
    char buf[40];
    for (int digits = 1; digits <= 17; ++digits) {
        std::snprintf(buf, sizeof buf, "%.*g", digits, value);
        double back = std::strtod(buf, nullptr);
        if (isFloat ? static_cast<float>(back) == static_cast<float>(value) : back == value) break;
    }
    return buf;
}

std::string signedText(long long v) { return std::to_string(v); }
std::string unsignedText(unsigned long long v) { return std::to_string(v); }

bool ownedByBase(GParamSpec* spec) {
    return spec->owner_type == G_TYPE_OBJECT || spec->owner_type == GST_TYPE_OBJECT || spec->owner_type == GST_TYPE_PAD;
}

std::vector<PropertyInfo> classProperties(GObjectClass* klass) {
    guint n = 0;
    GParamSpec** specs = g_object_class_list_properties(klass, &n);
    std::vector<PropertyInfo> out;
    for (guint i = 0; i < n; ++i) {
        if (!ownedByBase(specs[i])) out.push_back(describeProperty(specs[i]));
    }
    g_free(specs);
    std::sort(out.begin(), out.end(), [](const PropertyInfo& a, const PropertyInfo& b) { return a.name < b.name; });
    return out;
}

bool startsWith(const std::string& text, const char* prefix) { return text.rfind(prefix, 0) == 0; }

FactoryEntry entryOf(GstElementFactory* factory) {
    FactoryEntry e;
    GstPluginFeature* feature = GST_PLUGIN_FEATURE(factory);
    e.factory = gst_plugin_feature_get_name(feature);
    e.plugin = orEmpty(gst_plugin_feature_get_plugin_name(feature));
    e.rank = static_cast<int>(gst_plugin_feature_get_rank(feature));
    e.klass = orEmpty(gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS));
    e.longName = orEmpty(gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_LONGNAME));
    bool hasSink = false, hasSrc = false;
    for (const GList* t = gst_element_factory_get_static_pad_templates(factory); t; t = t->next) {
        const GstStaticPadTemplate* tmpl = static_cast<const GstStaticPadTemplate*>(t->data);
        if (tmpl->direction == GST_PAD_SINK) {
            hasSink = true;
            if (tmpl->presence == GST_PAD_ALWAYS) e.hasAlwaysSink = true;
            else if (tmpl->presence == GST_PAD_SOMETIMES) e.hasSometimesSink = true;
            else if (tmpl->presence == GST_PAD_REQUEST) e.hasRequestSink = true;
        } else {
            hasSrc = true;
            if (tmpl->presence == GST_PAD_ALWAYS) e.hasAlwaysSrc = true;
            else if (tmpl->presence == GST_PAD_SOMETIMES) e.hasSometimesSrc = true;
            else if (tmpl->presence == GST_PAD_REQUEST) e.hasRequestSrc = true;
        }
    }
    e.isSource = !hasSink;
    e.isSink = hasSink && !hasSrc;
    e.nvidia = startsWith(e.plugin, "nv") || startsWith(e.factory, "nv") || startsWith(e.plugin, "deepstream") ||
               startsWith(e.factory, "deepstream");
    return e;
}

std::string valueText(const GValue* v) {
    if (G_VALUE_HOLDS_STRING(v)) return orEmpty(g_value_get_string(v));
    return take(gst_value_serialize(v));
}

std::string typeName(const GValue* v) { return g_type_name(G_VALUE_TYPE(v)); }

CapsFieldInfo fieldOf(const char* name, const GValue* v) {
    CapsFieldInfo f;
    f.name = name;
    if (GST_VALUE_HOLDS_LIST(v)) {
        f.kind = "list";
        guint n = gst_value_list_get_size(v);
        for (guint i = 0; i < n; ++i) {
            const GValue* item = gst_value_list_get_value(v, i);
            if (i == 0) f.type = typeName(item);
            f.values.push_back(valueText(item));
        }
    } else if (GST_VALUE_HOLDS_INT_RANGE(v)) {
        f.kind = "range";
        f.type = "gint";
        f.values = {signedText(gst_value_get_int_range_min(v)), signedText(gst_value_get_int_range_max(v))};
        if (gst_value_get_int_range_step(v) != 1) f.values.push_back(signedText(gst_value_get_int_range_step(v)));
    } else if (GST_VALUE_HOLDS_INT64_RANGE(v)) {
        f.kind = "range";
        f.type = "gint64";
        f.values = {signedText(gst_value_get_int64_range_min(v)), signedText(gst_value_get_int64_range_max(v))};
        if (gst_value_get_int64_range_step(v) != 1) f.values.push_back(signedText(gst_value_get_int64_range_step(v)));
    } else if (GST_VALUE_HOLDS_DOUBLE_RANGE(v)) {
        f.kind = "range";
        f.type = "gdouble";
        f.values = {shortest(gst_value_get_double_range_min(v), false), shortest(gst_value_get_double_range_max(v), false)};
    } else if (GST_VALUE_HOLDS_FRACTION_RANGE(v)) {
        f.kind = "range";
        f.type = "GstFraction";
        f.values = {valueText(gst_value_get_fraction_range_min(v)), valueText(gst_value_get_fraction_range_max(v))};
    } else {
        f.kind = "fixed";
        f.type = typeName(v);
        f.values = {valueText(v)};
    }
    return f;
}

PadTemplateInfo templateOf(const GstStaticPadTemplate* stat, GstElementClass* klass) {
    PadTemplateInfo t;
    t.name = stat->name_template;
    t.direction = stat->direction == GST_PAD_SRC ? "src" : "sink";
    t.presence = stat->presence == GST_PAD_ALWAYS ? "always" : stat->presence == GST_PAD_SOMETIMES ? "sometimes" : "request";
    GstPadTemplate* tmpl = gst_element_class_get_pad_template(klass, stat->name_template);
    GstCaps* caps = tmpl ? gst_pad_template_get_caps(tmpl) : gst_static_caps_get(&const_cast<GstStaticPadTemplate*>(stat)->static_caps);
    t.caps = take(gst_caps_to_string(caps));
    t.structures = structuresOf(caps);
    gst_caps_unref(caps);
    if (tmpl) {
        GType padType = G_TYPE_NONE;
        g_object_get(tmpl, "gtype", &padType, NULL);
        if (padType != G_TYPE_NONE) {
            gpointer padClass = g_type_class_ref(padType);
            t.padProperties = classProperties(G_OBJECT_CLASS(padClass));
            g_type_class_unref(padClass);
        }
    }
    return t;
}

}  // namespace

PropertyInfo describeProperty(GParamSpec* spec) {
    PropertyInfo p;
    p.name = g_param_spec_get_name(spec);
    p.nick = orEmpty(g_param_spec_get_nick(spec));
    p.blurb = orEmpty(g_param_spec_get_blurb(spec));
    p.typeName = g_type_name(spec->value_type);
    p.readable = spec->flags & G_PARAM_READABLE;
    p.writable = spec->flags & G_PARAM_WRITABLE;
    p.construct = spec->flags & G_PARAM_CONSTRUCT;
    p.constructOnly = spec->flags & G_PARAM_CONSTRUCT_ONLY;
    p.deprecated = spec->flags & G_PARAM_DEPRECATED;
    p.mutableReady = spec->flags & GST_PARAM_MUTABLE_READY;
    p.mutablePaused = spec->flags & GST_PARAM_MUTABLE_PAUSED;
    p.mutablePlaying = spec->flags & GST_PARAM_MUTABLE_PLAYING;

    if (G_IS_PARAM_SPEC_BOOLEAN(spec)) {
        p.kind = "boolean";
        p.defaultValue = G_PARAM_SPEC_BOOLEAN(spec)->default_value ? "true" : "false";
    } else if (G_IS_PARAM_SPEC_INT(spec)) {
        GParamSpecInt* s = G_PARAM_SPEC_INT(spec);
        p.kind = "int";
        p.min = signedText(s->minimum), p.max = signedText(s->maximum), p.defaultValue = signedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_UINT(spec)) {
        GParamSpecUInt* s = G_PARAM_SPEC_UINT(spec);
        p.kind = "uint";
        p.min = unsignedText(s->minimum), p.max = unsignedText(s->maximum), p.defaultValue = unsignedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_INT64(spec)) {
        GParamSpecInt64* s = G_PARAM_SPEC_INT64(spec);
        p.kind = "int64";
        p.min = signedText(s->minimum), p.max = signedText(s->maximum), p.defaultValue = signedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_UINT64(spec)) {
        GParamSpecUInt64* s = G_PARAM_SPEC_UINT64(spec);
        p.kind = "uint64";
        p.min = unsignedText(s->minimum), p.max = unsignedText(s->maximum), p.defaultValue = unsignedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_LONG(spec)) {
        GParamSpecLong* s = G_PARAM_SPEC_LONG(spec);
        p.kind = "int64";
        p.min = signedText(s->minimum), p.max = signedText(s->maximum), p.defaultValue = signedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_ULONG(spec)) {
        GParamSpecULong* s = G_PARAM_SPEC_ULONG(spec);
        p.kind = "uint64";
        p.min = unsignedText(s->minimum), p.max = unsignedText(s->maximum), p.defaultValue = unsignedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_CHAR(spec)) {
        GParamSpecChar* s = G_PARAM_SPEC_CHAR(spec);
        p.kind = "int";
        p.min = signedText(s->minimum), p.max = signedText(s->maximum), p.defaultValue = signedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_UCHAR(spec)) {
        GParamSpecUChar* s = G_PARAM_SPEC_UCHAR(spec);
        p.kind = "uint";
        p.min = unsignedText(s->minimum), p.max = unsignedText(s->maximum), p.defaultValue = unsignedText(s->default_value);
    } else if (G_IS_PARAM_SPEC_FLOAT(spec)) {
        GParamSpecFloat* s = G_PARAM_SPEC_FLOAT(spec);
        p.kind = "float";
        p.min = shortest(s->minimum, true), p.max = shortest(s->maximum, true), p.defaultValue = shortest(s->default_value, true);
    } else if (G_IS_PARAM_SPEC_DOUBLE(spec)) {
        GParamSpecDouble* s = G_PARAM_SPEC_DOUBLE(spec);
        p.kind = "double";
        p.min = shortest(s->minimum, false), p.max = shortest(s->maximum, false), p.defaultValue = shortest(s->default_value, false);
    } else if (G_IS_PARAM_SPEC_STRING(spec)) {
        p.kind = "string";
        p.defaultValue = orEmpty(G_PARAM_SPEC_STRING(spec)->default_value);
    } else if (G_IS_PARAM_SPEC_ENUM(spec)) {
        GParamSpecEnum* s = G_PARAM_SPEC_ENUM(spec);
        p.kind = "enum";
        for (guint i = 0; i < s->enum_class->n_values; ++i) {
            const GEnumValue& v = s->enum_class->values[i];
            p.enumValues.push_back({v.value_nick, v.value_name, signedText(v.value)});
            if (v.value == s->default_value) p.defaultValue = v.value_nick;
        }
    } else if (G_IS_PARAM_SPEC_FLAGS(spec)) {
        GParamSpecFlags* s = G_PARAM_SPEC_FLAGS(spec);
        p.kind = "flags";
        for (guint i = 0; i < s->flags_class->n_values; ++i) {
            const GFlagsValue& v = s->flags_class->values[i];
            p.enumValues.push_back({v.value_nick, v.value_name, unsignedText(v.value)});
            if (v.value != 0 && (s->default_value & v.value) == v.value) {
                p.defaultValue += (p.defaultValue.empty() ? "" : "+") + std::string(v.value_nick);
            }
        }
        if (p.defaultValue.empty()) p.defaultValue = "0";
    } else if (GST_IS_PARAM_SPEC_FRACTION(spec)) {
        GstParamSpecFraction* s = GST_PARAM_SPEC_FRACTION(spec);
        p.kind = "fraction";
        p.min = std::to_string(s->min_num) + "/" + std::to_string(s->min_den);
        p.max = std::to_string(s->max_num) + "/" + std::to_string(s->max_den);
        p.defaultValue = std::to_string(s->def_num) + "/" + std::to_string(s->def_den);
    } else if (spec->value_type == GST_TYPE_CAPS) {
        p.kind = "caps";
    } else if (G_IS_PARAM_SPEC_OBJECT(spec)) {
        p.kind = "object";
    } else if (G_IS_PARAM_SPEC_BOXED(spec)) {
        p.kind = "boxed";
    } else if (G_IS_PARAM_SPEC_POINTER(spec)) {
        p.kind = "pointer";
    } else {
        p.kind = "other";
    }
    return p;
}

std::vector<StructureInfo> structuresOf(const GstCaps* caps) {
    std::vector<StructureInfo> out;
    if (!caps || gst_caps_is_any(caps) || gst_caps_is_empty(caps)) return out;
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        const GstStructure* s = gst_caps_get_structure(caps, i);
        StructureInfo info;
        info.media = gst_structure_get_name(s);
        GstCapsFeatures* features = gst_caps_get_features(caps, i);
        if (features && gst_caps_features_is_any(features)) {
            info.features.push_back("ANY");
        } else if (features && !gst_caps_features_is_equal(features, GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY)) {
            for (guint k = 0; k < gst_caps_features_get_size(features); ++k) {
                info.features.push_back(gst_caps_features_get_nth(features, k));
            }
        }
        for (gint k = 0; k < gst_structure_n_fields(s); ++k) {
            const char* name = gst_structure_nth_field_name(s, k);
            info.fields.push_back(fieldOf(name, gst_structure_get_value(s, name)));
        }
        out.push_back(info);
    }
    return out;
}

std::vector<FactoryEntry> listFactories() {
    std::vector<FactoryEntry> out;
    GList* all = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
    for (GList* l = all; l; l = l->next) out.push_back(entryOf(GST_ELEMENT_FACTORY(l->data)));
    gst_plugin_feature_list_free(all);
    std::sort(out.begin(), out.end(), [](const FactoryEntry& a, const FactoryEntry& b) { return a.factory < b.factory; });
    return out;
}

bool inspect(const std::string& name, ElementInfo* out, std::string* error) {
    GstElementFactory* found = gst_element_factory_find(name.c_str());
    if (!found) {
        if (error) *error = "element '" + name + "' was not found, its plugin may not be installed";
        return false;
    }
    GstPluginFeature* loaded = gst_plugin_feature_load(GST_PLUGIN_FEATURE(found));
    gst_object_unref(found);
    if (!loaded) {
        if (error) *error = "unable to load the plugin of element '" + name + "', its libraries may be missing";
        return false;
    }
    GstElementFactory* factory = GST_ELEMENT_FACTORY(loaded);
    gpointer klass = g_type_class_ref(gst_element_factory_get_element_type(factory));

    ElementInfo info;
    info.entry = entryOf(factory);
    info.description = orEmpty(gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_DESCRIPTION));
    info.author = orEmpty(gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_AUTHOR));
    for (const GList* t = gst_element_factory_get_static_pad_templates(factory); t; t = t->next) {
        info.padTemplates.push_back(templateOf(static_cast<const GstStaticPadTemplate*>(t->data), GST_ELEMENT_CLASS(klass)));
    }
    info.properties = classProperties(G_OBJECT_CLASS(klass));
    g_type_class_unref(klass);
    gst_object_unref(loaded);
    *out = info;
    return true;
}

bool compatibleFactories(const std::string& capsText, std::vector<std::string>* out, std::string* error) {
    GstCaps* caps = nullptr;
    if (!capsText.empty()) {
        caps = gst_caps_from_string(capsText.c_str());
        if (!caps) {
            if (error) *error = "the caps text cannot be read: '" + capsText + "'";
            return false;
        }
    }
    // Not gst_element_factory_list_get_elements(TYPE_ANY): it keeps only factories that have every type bit set.
    GList* all = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
    GList* fit = nullptr;
    if (caps) {
        fit = gst_element_factory_list_filter(all, caps, GST_PAD_SINK, FALSE);
        gst_caps_unref(caps);
    } else {
        for (GList* l = all; l; l = l->next) {
            GstElementFactory* f = GST_ELEMENT_FACTORY(l->data);
            if (gst_element_factory_get_num_pad_templates(f) == 0) continue;
            for (const GList* t = gst_element_factory_get_static_pad_templates(f); t; t = t->next) {
                if (static_cast<const GstStaticPadTemplate*>(t->data)->direction == GST_PAD_SINK) {
                    fit = g_list_prepend(fit, gst_object_ref(f));
                    break;
                }
            }
        }
    }
    gst_plugin_feature_list_free(all);
    fit = g_list_sort(fit, gst_plugin_feature_rank_compare_func);  // rank first, then name
    out->clear();
    for (GList* l = fit; l; l = l->next) out->push_back(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(l->data)));
    gst_plugin_feature_list_free(fit);
    return true;
}

bool parseCaps(const std::string& text, std::string* normalized, std::vector<StructureInfo>* out, std::string* error) {
    GstCaps* caps = text.empty() ? nullptr : gst_caps_from_string(text.c_str());
    if (!caps) {
        if (error) *error = "the caps text cannot be read: '" + text + "'";
        return false;
    }
    *normalized = take(gst_caps_to_string(caps));
    *out = structuresOf(caps);
    gst_caps_unref(caps);
    return true;
}

// ---- JSON

namespace {

void writeProperty(JsonWriter& w, const PropertyInfo& p) {
    w.beginObject();
    w.field("name", p.name).field("nick", p.nick).field("blurb", p.blurb).field("kind", p.kind);
    w.field("typeName", p.typeName).field("default", p.defaultValue).field("min", p.min).field("max", p.max);
    w.key("enumValues").beginArray();
    for (const EnumValue& v : p.enumValues) {
        w.beginObject().field("nick", v.nick).field("name", v.name).field("value", v.value).end();
    }
    w.end();
    w.field("readable", p.readable).field("writable", p.writable).field("construct", p.construct);
    w.field("constructOnly", p.constructOnly).field("deprecated", p.deprecated);
    w.field("mutableReady", p.mutableReady).field("mutablePaused", p.mutablePaused).field("mutablePlaying", p.mutablePlaying);
    w.end();
}

}  // namespace

void writeStructure(JsonWriter& w, const StructureInfo& s) {
    w.beginObject().field("media", s.media);
    w.key("features").beginArray();
    for (const std::string& f : s.features) w.string(f);
    w.end();
    w.key("fields").beginArray();
    for (const CapsFieldInfo& f : s.fields) {
        w.beginObject().field("name", f.name).field("type", f.type).field("kind", f.kind);
        w.key("values").beginArray();
        for (const std::string& v : f.values) w.string(v);
        w.end().end();
    }
    w.end().end();
}

std::string listJson(const std::vector<FactoryEntry>& entries) {
    JsonWriter w;
    w.beginObject().key("elements").beginArray();
    for (const FactoryEntry& e : entries) {
        w.beginObject().field("factory", e.factory).field("klass", e.klass).field("longName", e.longName);
        w.field("rank", e.rank).field("plugin", e.plugin).field("isSource", e.isSource).field("isSink", e.isSink);
        w.field("hasAlwaysSrc", e.hasAlwaysSrc).field("hasAlwaysSink", e.hasAlwaysSink);
        w.field("hasSometimesSrc", e.hasSometimesSrc).field("hasSometimesSink", e.hasSometimesSink);
        w.field("hasRequestSrc", e.hasRequestSrc).field("hasRequestSink", e.hasRequestSink);
        w.field("nvidia", e.nvidia).end();
    }
    w.end().end();
    return w.str();
}

std::string elementJson(const ElementInfo& info) {
    JsonWriter w;
    w.beginObject();
    w.field("factory", info.entry.factory).field("longName", info.entry.longName).field("klass", info.entry.klass);
    w.field("description", info.description).field("author", info.author).field("plugin", info.entry.plugin);
    w.field("rank", info.entry.rank).field("isSource", info.entry.isSource).field("isSink", info.entry.isSink);
    w.key("padTemplates").beginArray();
    for (const PadTemplateInfo& t : info.padTemplates) {
        w.beginObject().field("name", t.name).field("direction", t.direction).field("presence", t.presence).field("caps", t.caps);
        w.key("structures").beginArray();
        for (const StructureInfo& s : t.structures) writeStructure(w, s);
        w.end();
        w.key("padProperties").beginArray();
        for (const PropertyInfo& p : t.padProperties) writeProperty(w, p);
        w.end().end();
    }
    w.end();
    w.key("properties").beginArray();
    for (const PropertyInfo& p : info.properties) writeProperty(w, p);
    w.end().end();
    return w.str();
}

std::string versionJson(const std::vector<FactoryEntry>& entries) {
    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);
    int nvidia = 0;
    for (const FactoryEntry& e : entries) nvidia += e.nvidia ? 1 : 0;
    GList* plugins = gst_registry_get_plugin_list(gst_registry_get());
    int pluginCount = static_cast<int>(g_list_length(plugins));
    gst_plugin_list_free(plugins);
    JsonWriter w;
    w.beginObject().key("gstreamer").beginObject();
    w.field("version", std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(micro));
    w.field("major", static_cast<int>(major)).field("minor", static_cast<int>(minor)).field("micro", static_cast<int>(micro)).end();
    w.key("nvidia").beginObject().field("present", nvidia > 0).field("elements", nvidia).end();
    w.key("plugins").beginObject().field("count", pluginCount).field("elements", static_cast<int>(entries.size())).end();
    w.end();
    return w.str();
}

std::string compatibleJson(const std::vector<std::string>& factories) {
    JsonWriter w;
    w.beginObject().key("factories").beginArray();
    for (const std::string& f : factories) w.string(f);
    w.end().end();
    return w.str();
}

std::string capsJson(bool ok, const std::string& textOrError, const std::vector<StructureInfo>& structures) {
    JsonWriter w;
    w.beginObject().field("ok", ok);
    if (!ok) {
        w.field("error", textOrError);
    } else {
        w.field("text", textOrError);
        w.key("structures").beginArray();
        for (const StructureInfo& s : structures) writeStructure(w, s);
        w.end();
    }
    w.end();
    return w.str();
}

}  // namespace gps
