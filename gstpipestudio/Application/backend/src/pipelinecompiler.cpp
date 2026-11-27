#include "pipelinecompiler.h"

#include "pluginlibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

QString cEscape(const std::string& text) {
    QString out;
    out.reserve(static_cast<int>(text.size()) + 8);
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                out += QString("\\x%1").arg(c, 2, 16, QLatin1Char('0'));
            } else {
                out += QChar(c);
            }
            break;
        }
    }
    out += '"';
    return out;
}

QString cEscape(const QString& text) { return cEscape(text.toStdString()); }

bool writeTextFile(const QString& path, const QString& content, QString* error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = "Cannot create directory for " + path;
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = "Cannot open " + path + ": " + file.errorString();
        return false;
    }
    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = "Cannot write " + path + ": " + file.errorString();
        return false;
    }
    return true;
}

bool copyFileOverwrite(const QString& src, const QString& dst, QString* error) {
    if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
        if (error) *error = "Cannot create directory for " + dst;
        return false;
    }
    if (QFileInfo::exists(dst)) QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        if (error) *error = "Cannot copy " + src + " to " + dst;
        return false;
    }
    return true;
}

QString findRuntimeSdkDir() {
    auto valid = [](const QString& path) {
        return !path.isEmpty() && QFileInfo::exists(QDir(path).filePath("include/gps_runtime/abi.h")) &&
               QFileInfo::exists(QDir(path).filePath("include/gps_runtime/pipeline.hpp"));
    };
    const QString env = qEnvironmentVariable("GPS_RUNTIME_SDK_DIR");
    if (valid(env)) return QDir(env).absolutePath();
    const QString installed = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../runtime_sdk");
    if (valid(installed)) return installed;
#ifdef GPS_RUNTIME_SDK_DIR
    const QString compiled = QStringLiteral(GPS_RUNTIME_SDK_DIR);
    if (valid(compiled)) return QDir(compiled).absolutePath();
#endif
    return {};
}

struct BoundaryPort {
    QString name;
    QString element;
    QString pad;
    bool isSink = false;
    QString caps;
};

std::vector<BoundaryPort> collectBoundaryPorts(const gps::Graph& originalGraph, const gps::Graph& flatGraph) {
    std::map<std::string, std::vector<gps::Pad>> padsByElem;
    for (const auto& el : originalGraph.elements) {
        if (!el.pads.empty()) padsByElem[el.name] = el.pads;
    }

    std::vector<BoundaryPort> ports;
    QSet<QString> usedNames;
    auto uniquePortName = [&](const QString& base) {
        QString candidate = base;
        int n = 1;
        while (usedNames.contains(candidate)) candidate = base + "_" + QString::number(n++);
        usedNames.insert(candidate);
        return candidate;
    };

    for (const auto& el : flatGraph.elements) {
        auto it = padsByElem.find(el.name);
        if (it == padsByElem.end()) continue;
        for (const auto& pad : it->second) {
            const QString caps = QString::fromStdString(pad.negotiatedCaps.empty() ? pad.templateCaps : pad.negotiatedCaps);
            if (pad.direction == gps::Pad::Direction::Sink && pad.presence == gps::Pad::Presence::Always) {
                if (flatGraph.linkInto(el.name, pad.identity) < 0) {
                    const QString base = QString::fromStdString(el.name + "_" + pad.identity);
                    ports.push_back({uniquePortName(base), QString::fromStdString(el.name),
                                     QString::fromStdString(pad.identity), true, caps});
                }
            } else if (pad.direction == gps::Pad::Direction::Source && pad.presence != gps::Pad::Presence::Request) {
                if (flatGraph.linkFrom(el.name, pad.identity) < 0) {
                    const QString base = QString::fromStdString(el.name + "_" + pad.identity);
                    ports.push_back({uniquePortName(base), QString::fromStdString(el.name),
                                     QString::fromStdString(pad.identity), false, caps});
                }
            }
        }
    }
    return ports;
}

QString generateCMakeLists(const QString& moduleName) {
    return QString(
               "cmake_minimum_required(VERSION 3.16)\n"
               "project(%1 LANGUAGES CXX)\n\n"
               "set(CMAKE_CXX_STANDARD 17)\n"
               "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
               "set(CMAKE_CXX_EXTENSIONS OFF)\n\n"
               "find_package(PkgConfig REQUIRED)\n"
               "pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0)\n\n"
               "add_library(%1 SHARED %1_module.cpp)\n"
               "set_target_properties(%1 PROPERTIES PREFIX \"lib\" OUTPUT_NAME \"%1\")\n"
               "target_include_directories(%1 PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/include\")\n"
               "target_link_libraries(%1 PRIVATE PkgConfig::GST ${CMAKE_DL_LIBS})\n"
               "target_compile_options(%1 PRIVATE -Wall -Wextra)\n")
        .arg(moduleName);
}

QString generateModuleSource(const gps::Graph& flatGraph, const std::vector<BoundaryPort>& ports,
                             const PipelineExportOptions& opts) {
    QString s;
    s += "// Auto-generated by GstPipeStudio PipelineCompiler. Dual-personality module:\n";
    s += "//   1. runtime_sdk C99 ABI: gps_pipeline_get_module_v1()\n";
    s += "//   2. Native GStreamer plugin (GstBin subclass): GST_PLUGIN_DEFINE(" + opts.moduleName + ")\n";
    s += "#include \"gps_runtime/abi.h\"\n\n";
    s += "#include <gst/gst.h>\n";
    s += "#include <dlfcn.h>\n";
    s += "#include <dirent.h>\n";
    s += "#include <sys/stat.h>\n\n";
    s += "#include <algorithm>\n";
    s += "#include <chrono>\n";
    s += "#include <cstring>\n";
    s += "#include <map>\n";
    s += "#include <mutex>\n";
    s += "#include <string>\n";
    s += "#include <vector>\n\n";

    s += "extern \"C\" const GpsPipelineModuleV1* gps_pipeline_get_module_v1(void);\n\n";
    s += "namespace {\n\n";

    // Static graph metadata
    s += "struct PropKV { const char* key; const char* value; };\n";
    s += "struct ReqPad { const char* tmpl; int count; };\n";
    s += "struct ElemDesc {\n";
    s += "    const char* name;\n";
    s += "    const char* factory;\n";
    s += "    const PropKV* props;\n";
    s += "    size_t prop_count;\n";
    s += "    const ReqPad* req_srcs;\n";
    s += "    size_t req_src_count;\n";
    s += "};\n";
    s += "struct LinkDesc {\n";
    s += "    const char* src_elem;\n";
    s += "    const char* src_pad;\n";
    s += "    const char* sink_elem;\n";
    s += "    const char* sink_pad;\n";
    s += "    const char* caps;\n";
    s += "};\n";
    s += "struct PortMeta {\n";
    s += "    const char* name;\n";
    s += "    const char* elem;\n";
    s += "    const char* pad;\n";
    s += "    GpsRuntimePortDirection direction;\n";
    s += "    const char* caps;\n";
    s += "};\n\n";

    // Emit element property and request pad arrays
    for (size_t i = 0; i < flatGraph.elements.size(); ++i) {
        const auto& el = flatGraph.elements[i];
        if (!el.props.empty()) {
            s += QString("const PropKV kElemProps_%1[] = {\n").arg(i);
            for (const auto& kv : el.props) {
                s += QString("    {%1, %2},\n").arg(cEscape(kv.first), cEscape(kv.second));
            }
            s += "};\n";
        }
        if (!el.requestSrc.empty()) {
            s += QString("const ReqPad kElemReqSrc_%1[] = {\n").arg(i);
            for (const auto& rq : el.requestSrc) {
                s += QString("    {%1, %2},\n").arg(cEscape(rq.first)).arg(rq.second);
            }
            s += "};\n";
        }
    }

    s += "const ElemDesc kElements[] = {\n";
    for (size_t i = 0; i < flatGraph.elements.size(); ++i) {
        const auto& el = flatGraph.elements[i];
        const QString propsPtr = el.props.empty() ? "nullptr" : QString("kElemProps_%1").arg(i);
        const QString reqPtr = el.requestSrc.empty() ? "nullptr" : QString("kElemReqSrc_%1").arg(i);
        s += QString("    {%1, %2, %3, %4, %5, %6},\n")
                 .arg(cEscape(el.name), cEscape(el.factory), propsPtr)
                 .arg(el.props.size())
                 .arg(reqPtr)
                 .arg(el.requestSrc.size());
    }
    s += "};\n";
    s += QString("constexpr size_t kElementCount = %1;\n\n").arg(flatGraph.elements.size());

    s += "const LinkDesc kLinks[] = {\n";
    for (const auto& l : flatGraph.links) {
        s += QString("    {%1, %2, %3, %4, %5},\n")
                 .arg(cEscape(l.srcElement), cEscape(l.srcPad), cEscape(l.sinkElement), cEscape(l.sinkPad), cEscape(l.caps));
    }
    s += "};\n";
    s += QString("constexpr size_t kLinkCount = %1;\n\n").arg(flatGraph.links.size());

    if (!flatGraph.exposed.empty()) {
        s += "const GpsRuntimeParamDesc kParams[] = {\n";
        for (const auto& p : flatGraph.exposed) {
            std::string defVal;
            if (const auto* el = flatGraph.find(p.element)) {
                auto it = el->props.find(p.property);
                if (it != el->props.end()) defVal = it->second;
            }
            s += QString("    {%1, %2, %3, GPS_PARAM_STRING, %4},\n")
                     .arg(cEscape(p.name), cEscape(p.element), cEscape(p.property), cEscape(defVal));
        }
        s += "};\n";
    } else {
        s += "const GpsRuntimeParamDesc* kParams = nullptr;\n";
    }
    s += QString("constexpr size_t kParamCount = %1;\n\n").arg(flatGraph.exposed.size());

    if (!ports.empty()) {
        s += "const GpsRuntimePortDesc kPorts[] = {\n";
        for (const auto& port : ports) {
            s += QString("    {%1, %2, %3},\n")
                     .arg(cEscape(port.name), port.isSink ? "GPS_PORT_SINK" : "GPS_PORT_SRC", cEscape(port.caps));
        }
        s += "};\n";
        s += "const PortMeta kPortMeta[] = {\n";
        for (const auto& port : ports) {
            s += QString("    {%1, %2, %3, %4, %5},\n")
                     .arg(cEscape(port.name), cEscape(port.element), cEscape(port.pad),
                          port.isSink ? "GPS_PORT_SINK" : "GPS_PORT_SRC", cEscape(port.caps));
        }
        s += "};\n";
    } else {
        s += "const GpsRuntimePortDesc* kPorts = nullptr;\n";
        s += "const PortMeta* kPortMeta = nullptr;\n";
    }
    s += QString("constexpr size_t kPortCount = %1;\n\n").arg(ports.size());

    // Helper functions in generated C++
    s += R"CPP(void copyStr(char* buf, size_t cap, const std::string& text) {
    if (!buf || cap == 0) return;
    const size_t n = std::min(text.size(), cap - 1);
    std::memcpy(buf, text.data(), n);
    buf[n] = '\0';
}

void scanBundledPlugins() {
    static std::once_flag once;
    std::call_once(once, [] {
        Dl_info dli{};
        if (!dladdr(reinterpret_cast<const void*>(&gps_pipeline_get_module_v1), &dli) || !dli.dli_fname) return;
        std::string path(dli.dli_fname);
        const size_t slash = path.find_last_of('/');
        if (slash == std::string::npos) return;
        const std::string pluginsDir = path.substr(0, slash) + "/plugins";
        struct stat st{};
        if (stat(pluginsDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            if (GstRegistry* reg = gst_registry_get()) {
                gst_registry_scan_path(reg, pluginsDir.c_str());
            }
        }
    });
}

void ensureGstReady() {
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
    scanBundledPlugins();
}

GstPad* resolveSrcPad(GstElement* el, const char* padName) {
    if (!el || !padName) return nullptr;
    if (GstPad* p = gst_element_get_static_pad(el, padName)) return p;
    std::string name(padName);
    const size_t hash = name.find('#');
    if (hash != std::string::npos) {
        const std::string tmpl = name.substr(0, hash);
        const int idx = std::atoi(name.substr(hash + 1).c_str());
        int seen = 0;
        GstIterator* it = gst_element_iterate_src_pads(el);
        GValue item = G_VALUE_INIT;
        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstPad* p = GST_PAD(g_value_get_object(&item));
            GstPadTemplate* pt = gst_pad_get_pad_template(p);
            if (pt && std::string(GST_PAD_TEMPLATE_NAME_TEMPLATE(pt)) == tmpl) {
                if (seen == idx) {
                    gst_object_ref(p);
                    g_value_unset(&item);
                    gst_iterator_free(it);
                    return p;
                }
                ++seen;
            }
            g_value_unset(&item);
        }
        gst_iterator_free(it);
    }
    return nullptr;
}

GstPad* resolveSinkPad(GstElement* el, const char* padName) {
    if (!el || !padName) return nullptr;
    if (GstPad* p = gst_element_get_static_pad(el, padName)) return p;
    if (std::strchr(padName, '%')) {
        return gst_element_request_pad_simple(el, padName);
    }
    return nullptr;
}

struct PendingDynamicLink {
    GstElement* srcElem = nullptr;
    std::string srcPad;
    GstElement* targetElem = nullptr;
    std::string targetPad;
    std::string caps;
    GstBin* bin = nullptr;
};

void onDynamicPadAdded(GstElement* src, GstPad* pad, gpointer data) {
    auto* pending = static_cast<PendingDynamicLink*>(data);
    if (!pending || !pending->bin || !pending->targetElem) return;
    const gchar* actualName = GST_PAD_NAME(pad);
    if (!actualName) return;
    // Match exact name or template prefix (before '#')
    std::string expected = pending->srcPad;
    const size_t hash = expected.find('#');
    if (hash != std::string::npos) expected = expected.substr(0, hash);
    if (std::string(actualName) != pending->srcPad &&
        std::string(actualName).rfind(expected.substr(0, expected.find('%')), 0) != 0) {
        return;
    }
    GstPad* sinkPad = resolveSinkPad(pending->targetElem, pending->targetPad.c_str());
    if (!sinkPad) return;
    if (gst_pad_is_linked(sinkPad)) {
        gst_object_unref(sinkPad);
        return;
    }
    if (!pending->caps.empty()) {
        GstElement* cf = gst_element_factory_make("capsfilter", nullptr);
        GstCaps* c = gst_caps_from_string(pending->caps.c_str());
        if (cf && c) {
            g_object_set(G_OBJECT(cf), "caps", c, nullptr);
            gst_caps_unref(c);
            gst_bin_add(pending->bin, cf);
            gst_element_sync_state_with_parent(cf);
            GstPad* cfSink = gst_element_get_static_pad(cf, "sink");
            GstPad* cfSrc = gst_element_get_static_pad(cf, "src");
            gst_pad_link(pad, cfSink);
            gst_pad_link(cfSrc, sinkPad);
            gst_object_unref(cfSink);
            gst_object_unref(cfSrc);
            gst_object_unref(sinkPad);
            return;
        }
        if (c) gst_caps_unref(c);
        if (cf) gst_object_unref(cf);
    }
    gst_pad_link(pad, sinkPad);
    gst_object_unref(sinkPad);
    (void)src;
}

bool buildGraphIntoBin(GstBin* bin, std::vector<PendingDynamicLink*>* dynamicLinks, std::string* errorOut) {
    std::map<std::string, GstElement*> byName;
    for (size_t i = 0; i < kElementCount; ++i) {
        const ElemDesc& desc = kElements[i];
        GstElement* el = gst_element_factory_make(desc.factory, desc.name);
        if (!el) {
            if (errorOut) *errorOut = std::string("Failed to create element '") + desc.name + "' (factory '" + desc.factory + "')";
            return false;
        }
        for (size_t p = 0; p < desc.prop_count; ++p) {
            gst_util_set_object_arg(G_OBJECT(el), desc.props[p].key, desc.props[p].value);
        }
        if (!gst_bin_add(bin, el)) {
            if (errorOut) *errorOut = std::string("Failed to add element '") + desc.name + "' to bin";
            gst_object_unref(el);
            return false;
        }
        for (size_t r = 0; r < desc.req_src_count; ++r) {
            for (int c = 0; c < desc.req_srcs[r].count; ++c) {
                if (GstPad* rp = gst_element_request_pad_simple(el, desc.req_srcs[r].tmpl)) {
                    gst_object_unref(rp);
                }
            }
        }
        byName[desc.name] = el;
    }

    for (size_t i = 0; i < kLinkCount; ++i) {
        const LinkDesc& l = kLinks[i];
        GstElement* srcEl = byName[l.src_elem];
        GstElement* sinkEl = byName[l.sink_elem];
        if (!srcEl || !sinkEl) {
            if (errorOut) *errorOut = std::string("Missing element for link ") + l.src_elem + " -> " + l.sink_elem;
            return false;
        }
        GstPad* srcPad = resolveSrcPad(srcEl, l.src_pad);
        if (!srcPad) {
            auto* pend = new PendingDynamicLink{srcEl, l.src_pad, sinkEl, l.sink_pad, l.caps ? l.caps : "", bin};
            if (dynamicLinks) dynamicLinks->push_back(pend);
            g_signal_connect(srcEl, "pad-added", G_CALLBACK(onDynamicPadAdded), pend);
            continue;
        }
        GstPad* sinkPad = resolveSinkPad(sinkEl, l.sink_pad);
        if (!sinkPad) {
            gst_object_unref(srcPad);
            if (errorOut) *errorOut = std::string("Failed to resolve sink pad ") + l.sink_elem + "." + l.sink_pad;
            return false;
        }
        bool linked = false;
        if (l.caps && l.caps[0] != '\0') {
            std::string cfName = std::string("gps_caps_") + std::to_string(i);
            GstElement* cf = gst_element_factory_make("capsfilter", cfName.c_str());
            GstCaps* caps = gst_caps_from_string(l.caps);
            if (cf && caps) {
                g_object_set(G_OBJECT(cf), "caps", caps, nullptr);
                gst_caps_unref(caps);
                gst_bin_add(bin, cf);
                GstPad* cfSink = gst_element_get_static_pad(cf, "sink");
                GstPad* cfSrc = gst_element_get_static_pad(cf, "src");
                linked = (gst_pad_link(srcPad, cfSink) == GST_PAD_LINK_OK) &&
                         (gst_pad_link(cfSrc, sinkPad) == GST_PAD_LINK_OK);
                gst_object_unref(cfSink);
                gst_object_unref(cfSrc);
            } else {
                if (caps) gst_caps_unref(caps);
                if (cf) gst_object_unref(cf);
            }
        } else {
            linked = (gst_pad_link(srcPad, sinkPad) == GST_PAD_LINK_OK);
        }
        gst_object_unref(srcPad);
        gst_object_unref(sinkPad);
        if (!linked) {
            if (errorOut) *errorOut = std::string("Failed to link ") + l.src_elem + "." + l.src_pad + " -> " + l.sink_elem + "." + l.sink_pad;
            return false;
        }
    }
    return true;
}

bool setExposedParam(GstBin* bin, const char* name, const char* value, std::string* errorOut) {
    const GpsRuntimeParamDesc* target = nullptr;
    for (size_t i = 0; i < kParamCount; ++i) {
        if (std::strcmp(kParams[i].name, name) == 0) {
            target = &kParams[i];
            break;
        }
    }
    if (!target) {
        if (errorOut) *errorOut = std::string("Unknown parameter: ") + (name ? name : "");
        return false;
    }
    GstElement* el = gst_bin_get_by_name(bin, target->element);
    if (!el) {
        if (errorOut) *errorOut = std::string("Element not found: ") + target->element;
        return false;
    }
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(el), target->property);
    if (!spec) {
        gst_object_unref(el);
        if (errorOut) *errorOut = std::string("Property not found: ") + target->element + "." + target->property;
        return false;
    }
    gst_util_set_object_arg(G_OBJECT(el), target->property, value ? value : "");
    gst_object_unref(el);
    return true;
}

bool getExposedParam(GstBin* bin, const char* name, char* outBuf, size_t outCap) {
    if (!outBuf || outCap == 0) return false;
    const GpsRuntimeParamDesc* target = nullptr;
    for (size_t i = 0; i < kParamCount; ++i) {
        if (std::strcmp(kParams[i].name, name) == 0) {
            target = &kParams[i];
            break;
        }
    }
    if (!target) return false;
    GstElement* el = gst_bin_get_by_name(bin, target->element);
    if (!el) return false;
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(el), target->property);
    if (!spec || !(spec->flags & G_PARAM_READABLE)) {
        gst_object_unref(el);
        return false;
    }
    GValue val = G_VALUE_INIT;
    g_value_init(&val, spec->value_type);
    g_object_get_property(G_OBJECT(el), target->property, &val);
    gchar* str = G_VALUE_HOLDS_STRING(&val) ? g_value_dup_string(&val) : gst_value_serialize(&val);
    copyStr(outBuf, outCap, str ? str : "");
    g_free(str);
    g_value_unset(&val);
    gst_object_unref(el);
    return true;
}

}  // namespace

// ============================================================================
// Personality 1: runtime_sdk C99 ABI
// ============================================================================
struct GpsPipelineHandle {
    GstElement* pipeline = nullptr;
    std::vector<PendingDynamicLink*> dynamicLinks;
    std::map<std::string, GstElement*> appSrcs;
    struct SinkCallback {
        std::string portName;
        GpsRuntimeFrameCallback cb = nullptr;
        void* userData = nullptr;
        std::mutex* mutex = nullptr;
    };
    std::vector<SinkCallback*> sinkCallbacks;
    std::mutex cbMutex;
    GpsRuntimeLogCallback logCb = nullptr;
    void* logUserData = nullptr;
    std::chrono::steady_clock::time_point startTime;
    std::vector<GpsRuntimeElementMetric> metricsCache;
};

namespace {

void onFakesinkHandoff(GstElement* /*sink*/, GstBuffer* buf, GstPad* pad, gpointer userData) {
    auto* sc = static_cast<GpsPipelineHandle::SinkCallback*>(userData);
    if (!sc || !buf) return;
    GpsRuntimeFrameCallback cb = nullptr;
    void* ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(*sc->mutex);
        cb = sc->cb;
        ud = sc->userData;
    }
    if (!cb) return;
    GstMapInfo map{};
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) return;
    gchar* capsStr = nullptr;
    if (pad) {
        if (GstCaps* caps = gst_pad_get_current_caps(pad)) {
            capsStr = gst_caps_to_string(caps);
            gst_caps_unref(caps);
        }
    }
    GpsRuntimeFrame frame{};
    frame.data = map.data;
    frame.size = map.size;
    frame.pts_ns = GST_BUFFER_PTS_IS_VALID(buf) ? GST_BUFFER_PTS(buf) : 0;
    frame.duration_ns = GST_BUFFER_DURATION_IS_VALID(buf) ? GST_BUFFER_DURATION(buf) : 0;
    frame.caps = capsStr ? capsStr : "";
    cb(sc->portName.c_str(), &frame, ud);
    g_free(capsStr);
    gst_buffer_unmap(buf, &map);
}

void emitLog(GpsPipelineHandle* h, int level, const std::string& msg) {
    GpsRuntimeLogCallback cb = nullptr;
    void* ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(h->cbMutex);
        cb = h->logCb;
        ud = h->logUserData;
    }
    if (cb) cb(level, msg.c_str(), ud);
}

std::string drainBusError(GstElement* pipeline) {
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!bus) return {};
    std::string out;
    while (GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        if (err) {
            out = err->message ? err->message : "GStreamer error";
            g_error_free(err);
        }
        g_free(dbg);
        gst_message_unref(msg);
        if (!out.empty()) break;
    }
    gst_object_unref(bus);
    return out;
}

GpsPipelineHandle* abiCreate(char* errBuf, size_t errCap) {
    ensureGstReady();
    auto* h = new GpsPipelineHandle();
    h->pipeline = gst_pipeline_new()CPP";
    s += cEscape(opts.moduleName);
    s += R"CPP();
    if (!h->pipeline) {
        copyStr(errBuf, errCap, "gst_pipeline_new failed");
        delete h;
        return nullptr;
    }
    std::string buildErr;
    if (!buildGraphIntoBin(GST_BIN(h->pipeline), &h->dynamicLinks, &buildErr)) {
        copyStr(errBuf, errCap, buildErr);
        gst_object_unref(h->pipeline);
        for (auto* p : h->dynamicLinks) delete p;
        delete h;
        return nullptr;
    }

    // Attach appsrc / fakesink adapters for any unlinked boundary ports
    for (size_t i = 0; i < kPortCount; ++i) {
        const PortMeta& pm = kPortMeta[i];
        GstElement* target = gst_bin_get_by_name(GST_BIN(h->pipeline), pm.elem);
        if (!target) continue;
        if (pm.direction == GPS_PORT_SINK) {
            std::string srcName = std::string("gps_appsrc_") + pm.name;
            GstElement* appsrc = gst_element_factory_make("appsrc", srcName.c_str());
            if (appsrc) {
                g_object_set(G_OBJECT(appsrc), "format", GST_FORMAT_TIME, "is-live", FALSE, nullptr);
                if (pm.caps && pm.caps[0] != '\0') {
                    if (GstCaps* c = gst_caps_from_string(pm.caps)) {
                        g_object_set(G_OBJECT(appsrc), "caps", c, nullptr);
                        gst_caps_unref(c);
                    }
                }
                gst_bin_add(GST_BIN(h->pipeline), appsrc);
                GstPad* srcPad = gst_element_get_static_pad(appsrc, "src");
                GstPad* sinkPad = resolveSinkPad(target, pm.pad);
                if (srcPad && sinkPad) gst_pad_link(srcPad, sinkPad);
                if (srcPad) gst_object_unref(srcPad);
                if (sinkPad) gst_object_unref(sinkPad);
                h->appSrcs[pm.name] = appsrc;
            }
        } else {
            std::string sinkName = std::string("gps_fakesink_") + pm.name;
            GstElement* fakesink = gst_element_factory_make("fakesink", sinkName.c_str());
            if (fakesink) {
                g_object_set(G_OBJECT(fakesink), "signal-handoffs", TRUE, "sync", FALSE, nullptr);
                gst_bin_add(GST_BIN(h->pipeline), fakesink);
                GstPad* srcPad = resolveSrcPad(target, pm.pad);
                GstPad* sinkPad = gst_element_get_static_pad(fakesink, "sink");
                if (srcPad && sinkPad) gst_pad_link(srcPad, sinkPad);
                if (srcPad) gst_object_unref(srcPad);
                if (sinkPad) gst_object_unref(sinkPad);
                auto* sc = new GpsPipelineHandle::SinkCallback{pm.name, nullptr, nullptr, &h->cbMutex};
                h->sinkCallbacks.push_back(sc);
                g_signal_connect(fakesink, "handoff", G_CALLBACK(onFakesinkHandoff), sc);
            }
        }
        gst_object_unref(target);
    }

    h->metricsCache.reserve(kElementCount);
    for (size_t i = 0; i < kElementCount; ++i) {
        GpsRuntimeElementMetric m{};
        m.element = kElements[i].name;
        m.age_ms = -1.0;
        m.queue_ms = -1.0;
        m.processing_ms = -1.0;
        m.drop_rate_percent = -1.0;
        h->metricsCache.push_back(m);
    }
    return h;
}

void abiDestroy(GpsPipelineHandle* h) {
    if (!h) return;
    if (h->pipeline) {
        gst_element_set_state(h->pipeline, GST_STATE_NULL);
        gst_object_unref(h->pipeline);
        h->pipeline = nullptr;
    }
    for (auto* p : h->dynamicLinks) delete p;
    for (auto* sc : h->sinkCallbacks) delete sc;
    delete h;
}

int abiStart(GpsPipelineHandle* h, char* errBuf, size_t errCap) {
    if (!h || !h->pipeline) return 0;
    h->startTime = std::chrono::steady_clock::now();
    const GstStateChangeReturn ret = gst_element_set_state(h->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::string err = drainBusError(h->pipeline);
        if (err.empty()) err = "Failed to transition pipeline to PLAYING";
        copyStr(errBuf, errCap, err);
        emitLog(h, 3, err);
        return 0;
    }
    return 1;
}

int abiPause(GpsPipelineHandle* h, char* errBuf, size_t errCap) {
    if (!h || !h->pipeline) return 0;
    const GstStateChangeReturn ret = gst_element_set_state(h->pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::string err = drainBusError(h->pipeline);
        if (err.empty()) err = "Failed to transition pipeline to PAUSED";
        copyStr(errBuf, errCap, err);
        return 0;
    }
    return 1;
}

int abiStop(GpsPipelineHandle* h) {
    if (!h || !h->pipeline) return 0;
    gst_element_set_state(h->pipeline, GST_STATE_NULL);
    return 1;
}

int abiSendEos(GpsPipelineHandle* h) {
    if (!h || !h->pipeline) return 0;
    if (!h->appSrcs.empty()) {
        for (const auto& kv : h->appSrcs) {
            GstFlowReturn flow = GST_FLOW_OK;
            g_signal_emit_by_name(kv.second, "end-of-stream", &flow);
        }
        return 1;
    }
    return gst_element_send_event(h->pipeline, gst_event_new_eos()) ? 1 : 0;
}

int abiWaitForEos(GpsPipelineHandle* h, int64_t timeoutMs, char* errBuf, size_t errCap) {
    if (!h || !h->pipeline) return 0;
    GstBus* bus = gst_element_get_bus(h->pipeline);
    if (!bus) return 0;
    const GstClockTime timeout = timeoutMs < 0 ? GST_CLOCK_TIME_NONE : static_cast<GstClockTime>(timeoutMs) * GST_MSECOND;
    GstMessage* msg = gst_bus_timed_pop_filtered(bus, timeout, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    gst_object_unref(bus);
    if (!msg) {
        copyStr(errBuf, errCap, "Timed out waiting for EOS");
        return 0;
    }
    int ok = 1;
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError* err = nullptr;
        gchar* dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        std::string text = (err && err->message) ? err->message : "Pipeline error";
        copyStr(errBuf, errCap, text);
        emitLog(h, 3, text);
        if (err) g_error_free(err);
        g_free(dbg);
        ok = 0;
    }
    gst_message_unref(msg);
    return ok;
}

int abiSetParam(GpsPipelineHandle* h, const char* name, const char* value, char* errBuf, size_t errCap) {
    if (!h || !h->pipeline) return 0;
    std::string err;
    if (!setExposedParam(GST_BIN(h->pipeline), name, value, &err)) {
        copyStr(errBuf, errCap, err);
        return 0;
    }
    return 1;
}

int abiGetParam(GpsPipelineHandle* h, const char* name, char* outBuf, size_t outCap) {
    if (!h || !h->pipeline) return 0;
    return getExposedParam(GST_BIN(h->pipeline), name, outBuf, outCap) ? 1 : 0;
}

int abiPushFrame(GpsPipelineHandle* h, const char* portName, const uint8_t* data, size_t size, uint64_t ptsNs,
                 const char* caps, char* errBuf, size_t errCap) {
    if (!h || !h->pipeline || !portName) return 0;
    auto it = h->appSrcs.find(portName);
    if (it == h->appSrcs.end()) {
        copyStr(errBuf, errCap, std::string("Unknown sink port: ") + portName);
        return 0;
    }
    if (caps && caps[0] != '\0') {
        if (GstCaps* c = gst_caps_from_string(caps)) {
            g_object_set(G_OBJECT(it->second), "caps", c, nullptr);
            gst_caps_unref(c);
        }
    }
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (!buf) {
        copyStr(errBuf, errCap, "Failed to allocate GstBuffer");
        return 0;
    }
    if (size > 0 && data) gst_buffer_fill(buf, 0, data, size);
    GST_BUFFER_PTS(buf) = ptsNs;
    GstFlowReturn flow = GST_FLOW_OK;
    g_signal_emit_by_name(it->second, "push-buffer", buf, &flow);
    gst_buffer_unref(buf);
    if (flow != GST_FLOW_OK) {
        copyStr(errBuf, errCap, "appsrc push-buffer failed");
        return 0;
    }
    return 1;
}

void abiSetFrameCallback(GpsPipelineHandle* h, const char* portName, GpsRuntimeFrameCallback cb, void* userData) {
    if (!h || !portName) return;
    std::lock_guard<std::mutex> lock(h->cbMutex);
    for (auto* sc : h->sinkCallbacks) {
        if (sc->portName == portName) {
            sc->cb = cb;
            sc->userData = userData;
        }
    }
}

void abiSetLogCallback(GpsPipelineHandle* h, GpsRuntimeLogCallback cb, void* userData) {
    if (!h) return;
    std::lock_guard<std::mutex> lock(h->cbMutex);
    h->logCb = cb;
    h->logUserData = userData;
}

int abiGetMetrics(GpsPipelineHandle* h, GpsRuntimeMetrics* outMetrics) {
    if (!h || !outMetrics) return 0;
    const auto now = std::chrono::steady_clock::now();
    outMetrics->elapsed_ms = std::chrono::duration<double, std::milli>(now - h->startTime).count();
    outMetrics->elements = h->metricsCache.empty() ? nullptr : h->metricsCache.data();
    outMetrics->element_count = h->metricsCache.size();
    return 1;
}

const GpsPipelineModuleV1 kModule = {
    GPS_PIPELINE_ABI_VERSION,
    )CPP";
    s += cEscape(opts.moduleName) + ",\n    " + cEscape(opts.version) + ",\n";
    s += R"CPP(    kParams,
    kParamCount,
    kPorts,
    kPortCount,
    abiCreate,
    abiDestroy,
    abiStart,
    abiPause,
    abiStop,
    abiSendEos,
    abiWaitForEos,
    abiSetParam,
    abiGetParam,
    abiPushFrame,
    abiSetFrameCallback,
    abiSetLogCallback,
    abiGetMetrics,
};

}  // namespace

extern "C" const GpsPipelineModuleV1* gps_pipeline_get_module_v1(void) {
    return &kModule;
}

// ============================================================================
// Personality 2: Native GStreamer Plugin (GstBin subclass)
// ============================================================================
namespace {

struct BinClass {
    GstBinClass parent;
};

struct BinInstance {
    GstBin parent;
    std::vector<PendingDynamicLink*>* dynamicLinks;
    std::string* buildError;
};

gpointer gParentClass = nullptr;

GParamSpec* targetParamSpec(const GpsRuntimeParamDesc& param) {
    const char* factoryName = nullptr;
    for (size_t i = 0; i < kElementCount; ++i) {
        if (std::strcmp(kElements[i].name, param.element) == 0) {
            factoryName = kElements[i].factory;
            break;
        }
    }
    if (!factoryName) return nullptr;
    GstElementFactory* factory = gst_element_factory_find(factoryName);
    if (!factory) return nullptr;
    GstPluginFeature* loaded = gst_plugin_feature_load(GST_PLUGIN_FEATURE(factory));
    gst_object_unref(factory);
    if (!loaded) return nullptr;
    gpointer klass = g_type_class_ref(gst_element_factory_get_element_type(GST_ELEMENT_FACTORY(loaded)));
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_CLASS(klass), param.property);
    g_type_class_unref(klass);
    gst_object_unref(loaded);
    return spec;
}

GParamSpec* cloneParamSpec(const char* name, GParamSpec* t) {
    const gchar* nick = t ? g_param_spec_get_nick(t) : name;
    const gchar* blurb = (t && g_param_spec_get_blurb(t)) ? g_param_spec_get_blurb(t) : name;
    GParamFlags flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    if (!t || !(t->flags & G_PARAM_WRITABLE)) return g_param_spec_string(name, nick, blurb, nullptr, flags);
    flags = static_cast<GParamFlags>(flags | (t->flags & (GST_PARAM_MUTABLE_READY | GST_PARAM_MUTABLE_PAUSED | GST_PARAM_MUTABLE_PLAYING)));
    switch (G_TYPE_FUNDAMENTAL(t->value_type)) {
    case G_TYPE_BOOLEAN: return g_param_spec_boolean(name, nick, blurb, G_PARAM_SPEC_BOOLEAN(t)->default_value, flags);
    case G_TYPE_INT: { auto* p = G_PARAM_SPEC_INT(t); return g_param_spec_int(name, nick, blurb, p->minimum, p->maximum, p->default_value, flags); }
    case G_TYPE_UINT: { auto* p = G_PARAM_SPEC_UINT(t); return g_param_spec_uint(name, nick, blurb, p->minimum, p->maximum, p->default_value, flags); }
    case G_TYPE_INT64: { auto* p = G_PARAM_SPEC_INT64(t); return g_param_spec_int64(name, nick, blurb, p->minimum, p->maximum, p->default_value, flags); }
    case G_TYPE_UINT64: { auto* p = G_PARAM_SPEC_UINT64(t); return g_param_spec_uint64(name, nick, blurb, p->minimum, p->maximum, p->default_value, flags); }
    case G_TYPE_FLOAT: { auto* p = G_PARAM_SPEC_FLOAT(t); return g_param_spec_float(name, nick, blurb, p->minimum, p->maximum, p->default_value, flags); }
    case G_TYPE_DOUBLE: { auto* p = G_PARAM_SPEC_DOUBLE(t); return g_param_spec_double(name, nick, blurb, p->minimum, p->maximum, p->default_value, flags); }
    case G_TYPE_STRING: return g_param_spec_string(name, nick, blurb, G_PARAM_SPEC_STRING(t)->default_value, flags);
    case G_TYPE_ENUM: return g_param_spec_enum(name, nick, blurb, t->value_type, G_PARAM_SPEC_ENUM(t)->default_value, flags);
    case G_TYPE_FLAGS: return g_param_spec_flags(name, nick, blurb, t->value_type, G_PARAM_SPEC_FLAGS(t)->default_value, flags);
    default: return g_param_spec_string(name, nick, blurb, nullptr, flags);
    }
}

void binSetProp(GObject* object, guint id, const GValue* value, GParamSpec*) {
    if (id == 0 || id > kParamCount) return;
    const GpsRuntimeParamDesc& param = kParams[id - 1];
    gchar* text = G_VALUE_HOLDS_STRING(value) ? g_value_dup_string(value) : gst_value_serialize(value);
    setExposedParam(GST_BIN(object), param.name, text ? text : "", nullptr);
    g_free(text);
}

void binGetProp(GObject* object, guint id, GValue* value, GParamSpec*) {
    if (id == 0 || id > kParamCount) return;
    const GpsRuntimeParamDesc& param = kParams[id - 1];
    GstElement* el = gst_bin_get_by_name(GST_BIN(object), param.element);
    if (!el) return;
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(el), param.property);
    if (spec && (spec->flags & G_PARAM_READABLE)) {
        GValue cur = G_VALUE_INIT;
        g_value_init(&cur, spec->value_type);
        g_object_get_property(G_OBJECT(el), param.property, &cur);
        if (G_VALUE_TYPE(&cur) == G_VALUE_TYPE(value)) {
            g_value_copy(&cur, value);
        } else {
            gchar* str = gst_value_serialize(&cur);
            g_value_take_string(value, str);
        }
        g_value_unset(&cur);
    }
    gst_object_unref(el);
}

GstStateChangeReturn binChangeState(GstElement* element, GstStateChange transition) {
    auto* self = reinterpret_cast<BinInstance*>(element);
    if (transition == GST_STATE_CHANGE_NULL_TO_READY && self->buildError && !self->buildError->empty()) {
        GST_ELEMENT_ERROR(element, CORE, FAILED, ("%s", self->buildError->c_str()), (nullptr));
        return GST_STATE_CHANGE_FAILURE;
    }
    return GST_ELEMENT_CLASS(gParentClass)->change_state(element, transition);
}

void binFinalize(GObject* object) {
    auto* self = reinterpret_cast<BinInstance*>(object);
    if (self->dynamicLinks) {
        for (auto* p : *self->dynamicLinks) delete p;
        delete self->dynamicLinks;
        self->dynamicLinks = nullptr;
    }
    delete self->buildError;
    self->buildError = nullptr;
    G_OBJECT_CLASS(gParentClass)->finalize(object);
}

void binClassInit(gpointer gClass, gpointer) {
    gParentClass = g_type_class_peek_parent(gClass);
    GObjectClass* objClass = G_OBJECT_CLASS(gClass);
    objClass->set_property = binSetProp;
    objClass->get_property = binGetProp;
    objClass->finalize = binFinalize;
    GstElementClass* elemClass = GST_ELEMENT_CLASS(gClass);
    elemClass->change_state = binChangeState;
    gst_element_class_set_metadata(elemClass,
                                   )CPP";
    const QString desc = opts.description.isEmpty() ? QString("Compiled GstPipeStudio pipeline: %1").arg(opts.moduleName)
                                                    : opts.description;
    s += cEscape(opts.moduleName) + ", \"Generic/Bin\", " + cEscape(desc) + ", \"GstPipeStudio\");\n";
    s += R"CPP(
    for (size_t i = 0; i < kParamCount; ++i) {
        const GpsRuntimeParamDesc& param = kParams[i];
        if (!g_object_class_find_property(objClass, param.name)) {
            GParamSpec* target = targetParamSpec(param);
            g_object_class_install_property(objClass, static_cast<guint>(i + 1), cloneParamSpec(param.name, target));
        }
    }

    for (size_t i = 0; i < kPortCount; ++i) {
        const PortMeta& pm = kPortMeta[i];
        GstCaps* caps = (pm.caps && pm.caps[0] != '\0') ? gst_caps_from_string(pm.caps) : gst_caps_new_any();
        if (!caps) caps = gst_caps_new_any();
        GstPadTemplate* tmpl = gst_pad_template_new(pm.name, pm.direction == GPS_PORT_SINK ? GST_PAD_SINK : GST_PAD_SRC,
                                                    GST_PAD_ALWAYS, caps);
        gst_element_class_add_pad_template(elemClass, tmpl);
        gst_caps_unref(caps);
    }
}

void binInstanceInit(GTypeInstance* instance, gpointer) {
    auto* self = reinterpret_cast<BinInstance*>(instance);
    self->dynamicLinks = new std::vector<PendingDynamicLink*>();
    self->buildError = new std::string();
    scanBundledPlugins();
    if (!buildGraphIntoBin(GST_BIN(self), self->dynamicLinks, self->buildError)) {
        return;
    }
    for (size_t i = 0; i < kPortCount; ++i) {
        const PortMeta& pm = kPortMeta[i];
        GstElement* target = gst_bin_get_by_name(GST_BIN(self), pm.elem);
        if (!target) continue;
        GstPad* pad = (pm.direction == GPS_PORT_SINK) ? resolveSinkPad(target, pm.pad) : resolveSrcPad(target, pm.pad);
        if (pad) {
            GstPad* ghost = gst_ghost_pad_new(pm.name, pad);
            if (ghost) {
                gst_pad_set_active(ghost, TRUE);
                gst_element_add_pad(GST_ELEMENT(self), ghost);
            }
            gst_object_unref(pad);
        }
        gst_object_unref(target);
    }
}

gboolean pluginInit(GstPlugin* plugin) {
    scanBundledPlugins();
    GTypeInfo info{};
    info.class_size = sizeof(BinClass);
    info.class_init = binClassInit;
    info.instance_size = sizeof(BinInstance);
    info.instance_init = binInstanceInit;
    const GType gtype = g_type_register_static(GST_TYPE_BIN, )CPP";
    s += cEscape("GpsBin_" + opts.moduleName);
    s += R"CPP(, &info, static_cast<GTypeFlags>(0));
    return gst_element_register(plugin, )CPP";
    s += cEscape(opts.moduleName);
    s += R"CPP(, GST_RANK_NONE, gtype);
}

}  // namespace

#define PACKAGE "gstpipestudio"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, )CPP";
    s += opts.moduleName + ", " + cEscape(desc) + ", pluginInit, " + cEscape(opts.version) +
         ", \"MIT/X11\", \"gstpipestudio\", \"https://github.com/mevlt01001/GstPipeStudio\")\n";
    return s;
}

QString generateExampleHost(const PipelineExportOptions& opts) {
    return QString(
               "#include \"gps_runtime/pipeline.hpp\"\n\n"
               "#include <iostream>\n\n"
               "int main(int argc, char** argv) {\n"
               "    const std::string soPath = (argc > 1) ? argv[1] : \"./lib%1.so\";\n"
               "    std::string err;\n"
               "    auto pipe = gps::runtime::Pipeline::open(soPath, &err);\n"
               "    if (!pipe) {\n"
               "        std::cerr << \"Open failed: \" << err << \"\\n\";\n"
               "        return 1;\n"
               "    }\n"
               "    std::cout << \"Loaded \" << pipe->manifest().name << \" v\" << pipe->manifest().version << \"\\n\";\n"
               "    if (!pipe->start(&err)) {\n"
               "        std::cerr << \"Start failed: \" << err << \"\\n\";\n"
               "        return 1;\n"
               "    }\n"
               "    if (!pipe->waitForEos(10000, &err)) {\n"
               "        std::cerr << \"Wait EOS: \" << err << \"\\n\";\n"
               "    }\n"
               "    pipe->stop();\n"
               "    return 0;\n"
               "}\n")
        .arg(opts.moduleName);
}

QString generateIntegrationDoc(const gps::Graph& flatGraph, const std::vector<BoundaryPort>& ports,
                               const PipelineExportOptions& opts) {
    QString doc;
    doc += QString("# %1 — Compiled Pipeline Integration Guide\n\n").arg(opts.moduleName);
    doc += QString("- **Shared Library:** `lib%1.so`\n").arg(opts.moduleName);
    doc += QString("- **Version:** `%1`\n").arg(opts.version);
    if (!opts.description.isEmpty()) doc += QString("- **Description:** %1\n").arg(opts.description);
    doc += "\n## Exposed Parameters\n\n";
    if (flatGraph.exposed.empty()) {
        doc += "_None_\n\n";
    } else {
        doc += "| Name | Target Element | Property |\n| --- | --- | --- |\n";
        for (const auto& p : flatGraph.exposed) {
            doc += QString("| `%1` | `%2` | `%3` |\n")
                       .arg(QString::fromStdString(p.name), QString::fromStdString(p.element),
                            QString::fromStdString(p.property));
        }
        doc += "\n";
    }

    doc += "## Boundary Ports\n\n";
    if (ports.empty()) {
        doc += "_None (self-contained pipeline)_\n\n";
    } else {
        doc += "| Port Name | Direction | Element.Pad | Caps |\n| --- | --- | --- | --- |\n";
        for (const auto& port : ports) {
            doc += QString("| `%1` | `%2` | `%3.%4` | `%5` |\n")
                       .arg(port.name, port.isSink ? "sink (input)" : "src (output)", port.element, port.pad,
                            port.caps.isEmpty() ? "ANY" : port.caps);
        }
        doc += "\n";
    }

    doc += "## C++ Host Usage (`gps::runtime::Pipeline`)\n\n```cpp\n";
    doc += QString("auto pipe = gps::runtime::Pipeline::open(\"./lib%1.so\", &err);\n").arg(opts.moduleName);
    for (const auto& p : flatGraph.exposed) {
        doc += QString("// pipe->setParam(\"%1\", \"<value>\", &err);\n").arg(QString::fromStdString(p.name));
    }
    doc += "pipe->start(&err);\npipe->waitForEos(10000, &err);\npipe->stop();\n```\n\n";
    doc += "Build `example_host.cpp`:\n```sh\n";
    doc += "g++ -std=c++17 example_host.cpp src/pipeline.cpp -Iinclude -ldl -lpthread -o example_host\n";
    doc += QString("./example_host ./lib%1.so\n```\n\n").arg(opts.moduleName);

    doc += "## Native GStreamer Plugin Usage\n\n```sh\n";
    doc += QString("GST_PLUGIN_PATH=. gst-inspect-1.0 %1\n").arg(opts.moduleName);
    doc += QString("GST_PLUGIN_PATH=. gst-launch-1.0 %1\n```\n\n").arg(opts.moduleName);

    doc += "## Host UI Team Integration Prompt\n\n```text\n";
    doc += QString("Integrate the compiled GStreamer/DeepStream pipeline `lib%1.so` into our host application using "
                   "`include/gps_runtime/pipeline.hpp` (class `gps::runtime::Pipeline`).\n"
                   "1. Load `./lib%1.so` via `gps::runtime::Pipeline::open(...)`.\n")
               .arg(opts.moduleName);
    if (!flatGraph.exposed.empty()) {
        doc += "2. Bind UI controls to these exposed parameters via `setParam(name, value)` / `getParam(name)`:\n";
        for (const auto& p : flatGraph.exposed) {
            doc += QString("   - %1 (%2.%3)\n")
                       .arg(QString::fromStdString(p.name), QString::fromStdString(p.element),
                            QString::fromStdString(p.property));
        }
    } else {
        doc += "2. No exposed parameters are required; call `start()`, `pause()`, `stop()`, and `metrics()`.\n";
    }
    if (!ports.empty()) {
        doc += "3. Wire boundary I/O ports:\n";
        for (const auto& port : ports) {
            doc += QString("   - %1 (%2): %3\n")
                       .arg(port.name, port.isSink ? "pushFrame" : "setFrameCallback",
                            port.caps.isEmpty() ? "ANY" : port.caps);
        }
    }
    doc += "```\n";
    return doc;
}

bool runProcess(const QString& program, const QStringList& args, const QString& workDir, QString* output, QString* error) {
    QProcess proc;
    proc.setWorkingDirectory(workDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");
    proc.setProcessEnvironment(env);
    proc.start(program, args);
    if (!proc.waitForStarted(10000)) {
        if (error) *error = "Failed to start " + program + ": " + proc.errorString();
        return false;
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        if (error) *error = program + " timed out.";
        return false;
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    if (output) *output += "$ " + program + " " + args.join(' ') + "\n" + out;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (error) *error = program + " failed (exit " + QString::number(proc.exitCode()) + ").";
        return false;
    }
    return true;
}

}  // namespace

QString PipelineCompiler::validateModuleName(const QString& name) {
    if (name.isEmpty()) return "Module name cannot be empty.";
    static const QRegularExpression pattern("^[a-z][a-z0-9_]*$");
    if (!pattern.match(name).hasMatch()) {
        return "Module name must start with a lowercase letter and contain only lowercase letters, digits, and underscores.";
    }
    return {};
}

QString PipelineCompiler::sanitizeModuleName(const QString& name) {
    QString out = name.toLower();
    out.replace(QRegularExpression("[^a-z0-9_]"), "_");
    out.replace(QRegularExpression("_+"), "_");
    while (out.startsWith('_')) out.remove(0, 1);
    while (out.endsWith('_')) out.chop(1);
    if (out.isEmpty() || !out[0].isLetter()) out.prepend("pipeline_");
    return out;
}

PipelineExportResult PipelineCompiler::compile(const gps::Graph& graph, const PipelineExportOptions& opts,
                                               const PluginLibrary* plugins) {
    PipelineExportResult res;
    if (const QString err = validateModuleName(opts.moduleName); !err.isEmpty()) {
        res.error = err;
        return res;
    }
    if (opts.outputDir.trimmed().isEmpty()) {
        res.error = "Output directory cannot be empty.";
        return res;
    }
    if (graph.empty()) {
        res.error = "Cannot export an empty pipeline.";
        return res;
    }
    if (!QDir().mkpath(opts.outputDir)) {
        res.error = "Cannot create output directory: " + opts.outputDir;
        return res;
    }

    // Flatten template instances via Inline conf round-trip.
    const std::string inlineConf = graph.toConf(gps::Graph::Form::Inline);
    gps::Graph flatGraph;
    std::string confErr;
    if (!gps::Graph::fromConf(inlineConf, &flatGraph, &confErr)) {
        res.error = "Failed to flatten pipeline: " + QString::fromStdString(confErr);
        return res;
    }
    flatGraph.instances.clear();
    for (auto& el : flatGraph.elements) el.group.clear();

    const std::vector<BoundaryPort> ports = collectBoundaryPorts(graph, flatGraph);

    // Copy custom plugin .so files used by factories in flatGraph into <outputDir>/plugins/
    if (plugins) {
        QSet<QString> copied;
        for (const std::string& factory : flatGraph.factories()) {
            const QString mod = plugins->publishedModule(QString::fromStdString(factory));
            if (mod.isEmpty() || !QFileInfo::exists(mod)) continue;
            const QString fileName = QFileInfo(mod).fileName();
            if (copied.contains(fileName)) continue;
            copied.insert(fileName);
            const QString dst = QDir(opts.outputDir).filePath("plugins/" + fileName);
            if (!copyFileOverwrite(mod, dst, &res.error)) return res;
        }
    }

    // Copy runtime_sdk headers (and pipeline.cpp if available) so outputDir is self-contained.
    if (opts.copyRuntimeHeaders) {
        const QString sdkDir = findRuntimeSdkDir();
        if (sdkDir.isEmpty()) {
            res.error = "runtime_sdk headers (gps_runtime/abi.h, pipeline.hpp) were not found.";
            return res;
        }
        const QString incOut = QDir(opts.outputDir).filePath("include/gps_runtime");
        if (!copyFileOverwrite(QDir(sdkDir).filePath("include/gps_runtime/abi.h"), QDir(incOut).filePath("abi.h"), &res.error))
            return res;
        if (!copyFileOverwrite(QDir(sdkDir).filePath("include/gps_runtime/pipeline.hpp"),
                               QDir(incOut).filePath("pipeline.hpp"), &res.error))
            return res;
        const QString pipelineCpp = QDir(sdkDir).filePath("src/pipeline.cpp");
        if (QFileInfo::exists(pipelineCpp)) {
            if (!copyFileOverwrite(pipelineCpp, QDir(opts.outputDir).filePath("src/pipeline.cpp"), &res.error)) return res;
        }
    }

    const QDir outDir(opts.outputDir);
    const QString moduleCppPath = outDir.filePath(opts.moduleName + "_module.cpp");
    const QString cmakePath = outDir.filePath("CMakeLists.txt");
    const QString integrationPath = outDir.filePath("INTEGRATION.md");
    const QString exampleHostPath = outDir.filePath("example_host.cpp");

    if (!writeTextFile(moduleCppPath, generateModuleSource(flatGraph, ports, opts), &res.error)) return res;
    if (!writeTextFile(cmakePath, generateCMakeLists(opts.moduleName), &res.error)) return res;
    if (!writeTextFile(integrationPath, generateIntegrationDoc(flatGraph, ports, opts), &res.error)) return res;
    if (!writeTextFile(exampleHostPath, generateExampleHost(opts), &res.error)) return res;

    const QString cmake = QStandardPaths::findExecutable("cmake");
    if (cmake.isEmpty()) {
        res.error = "cmake executable was not found in PATH.";
        return res;
    }

    const QString buildDir = outDir.filePath("build");
    QStringList configureArgs = {"-S", outDir.absolutePath(), "-B", buildDir, "-DCMAKE_BUILD_TYPE=RelWithDebInfo"};
    if (!QFileInfo::exists(QDir(buildDir).filePath("CMakeCache.txt"))) {
        if (!QStandardPaths::findExecutable("ninja").isEmpty()) configureArgs << "-G" << "Ninja";
    }
    if (!runProcess(cmake, configureArgs, outDir.absolutePath(), &res.output, &res.error)) return res;
    if (!runProcess(cmake, {"--build", buildDir, "--parallel"}, outDir.absolutePath(), &res.output, &res.error)) return res;

    const QString builtSo = QDir(buildDir).filePath("lib" + opts.moduleName + ".so");
    const QString finalSo = outDir.filePath("lib" + opts.moduleName + ".so");
    if (!QFileInfo::exists(builtSo)) {
        res.error = "Expected compiled library not found at " + builtSo;
        return res;
    }
    if (!copyFileOverwrite(builtSo, finalSo, &res.error)) return res;

    res.ok = true;
    res.soPath = finalSo;
    res.integrationDocPath = integrationPath;
    res.exampleHostPath = exampleHostPath;
    return res;
}

void PipelineCompiler::compileAsync(const gps::Graph& graph, const PipelineExportOptions& opts, QObject* context,
                                    OutputFn output, DoneFn done, const PluginLibrary* plugins) {
    QPointer<QObject> guard(context);
    auto* thread = QThread::create([graph, opts, plugins, guard, output = std::move(output), done = std::move(done)]() mutable {
        PipelineExportResult result = compile(graph, opts, plugins);
        if (!guard) return;
        QMetaObject::invokeMethod(
            guard.data(),
            [result = std::move(result), output = std::move(output), done = std::move(done)]() mutable {
                if (output && !result.output.isEmpty()) {
                    for (const QString& line : result.output.split('\n', Qt::SkipEmptyParts)) output(line);
                }
                if (done) done(std::move(result));
            },
            Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
