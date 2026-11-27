#include "pluginscaffolder.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

namespace {

const QString specMarker = "GPS_PLUGIN_SPEC";
const QString propertiesMarker = "GPS_PLUGIN_PROPERTIES";
const QString dependenciesMarker = "GPS_PLUGIN_DEPENDENCIES";

QString cppString(const QString& text) {
    // Octal byte escapes cannot consume digits from the following character.
    QString out = "\"";
    for (const unsigned char byte : text.toUtf8()) {
        if (byte == '"' || byte == '\\') out += '\\';
        if (byte >= 32 && byte <= 126) out += QLatin1Char(static_cast<char>(byte));
        else out += "\\" + QString::number(byte, 8).rightJustified(3, '0');
    }
    return out + '"';
}

QString boolLiteral(bool value) { return value ? "true" : "false"; }

QString caps(const PluginPortSpec& port) { return "readCaps(" + cppString(port.caps()) + ")"; }

QString portRegistration(const PluginPortSpec& p) {
    const QString args = cppString(p.name) + ", " + caps(p) + ", Presence::" + pluginPresenceNames().at(static_cast<int>(p.presence));
    if (p.direction == PluginDirection::Src) return "        b.addSrcPort(" + args + ");\n";
    return "        b.addSinkPort(" + args + ", InputMode::" + pluginInputNames().at(static_cast<int>(p.inputMode)) +
           ", " + QString::number(p.queueCapacity) + ", OverflowPolicy::" + pluginOverflowNames().at(static_cast<int>(p.overflowPolicy)) + ");\n";
}

QString memberOf(const PluginPropertySpec& p) {
    QString name = p.name;
    return "prop_" + name.replace('-', '_') + "_";
}

QStringList argumentsOf(const PluginPropertySpec& p) {
    auto args = p.cppArguments();
    if (p.type == PluginPropertyType::String) args[0] = "std::string(" + cppString(args[0]) + ")";
    return args;
}

QString properties(const PluginSpec& spec) {
    QString text;
    for (const auto& property : spec.properties)
        text += "    " + property.cppType() + " " + memberOf(property) + " = " + argumentsOf(property).front() + ";\n";
    return text;
}

QString describe(const PluginSpec& spec) {
    QString text = "    static constexpr const char* pluginDescription = " +
                   cppString(spec.description.isEmpty() ? spec.longName : spec.description) + ";\n" +
                   "    static constexpr const char* pluginVersion = " + cppString(spec.version) + ";\n" +
                   "    static constexpr const char* pluginLicense = " + cppString(spec.license) + ";\n\n" +
                   "    static void describe(ElementClassBuilder& b) {\n";
    text += "        b.setMetadata(" + cppString(spec.longName) + ", " + cppString(spec.category()) + ", pluginDescription, " +
            cppString(spec.author) + ");\n";
    for (const auto& port : spec.ports) text += portRegistration(port);
    for (const auto& p : spec.properties) {
        text += "        b.properties.prop(" + cppString(p.name) + ", &CustomElement::" + memberOf(p) + ", " +
                argumentsOf(p).join(", ") + ")\n";
        text += "            .nick(" + cppString(p.nick.isEmpty() ? p.name : p.nick) + ").blurb(" + cppString(p.blurb) + ")\n";
        text += "            .mutability(Mutability::" + pluginMutabilityNames().at(static_cast<int>(p.mutability)) +
                ").controllable(" + boolLiteral(p.controllable) + ");\n";
    }
    if (!spec.uriProtocol.isEmpty()) text += "        b.enableUriHandler(GST_URI_SRC, {" + cppString(spec.uriProtocol) + "});\n";
    return text + "    }\n";
}

QString sourceOptions(const PluginSpec& spec) {
    QString text = "    void configure() { setLiveSource(" + boolLiteral(spec.live) + "); }\n";
    text += "    bool start() override {\n        index_ = 0;\n        auto* src = srcPort(\"src\");\n";
    const auto start = spec.ports.front().capsPreset;
    QString defaults = "application/octet-stream";
    if (start == PluginCaps::VideoRaw)
        defaults = "video/x-raw,width=64,height=64,framerate=30/1,format=(string){RGBA,GRAY8,BGRx,RGB,I420,NV12}";
    if (start == PluginCaps::AudioRaw)
        defaults = "audio/x-raw,rate=48000,channels=1,layout=interleaved,format=(string){F32LE,S16LE,S32LE,U8}";
    text += "        auto example = src->templateCaps().intersect(readCaps(" + cppString(defaults) + "));\n";
    text += "        if (example.isEmpty()) example = src->templateCaps();\n"
            "        example = example.fixate();\n"
            "        if (!example.isFixed()) return false;\n"
            "        src->setCaps(example);\n"
            "        bytes_ = 16;\n"
            "        if (auto info = VideoInfo::fromCaps(example)) bytes_ = info->size();\n"
            "        if (auto info = AudioInfo::fromCaps(example)) bytes_ = 480 * info->bpf();\n"
            "        return SourceElement::start();\n    }\n";
    if (!spec.uriProtocol.isEmpty()) {
        text += "    std::string currentUri() const override { return uri_; }\n"
                "    bool setUri(const std::string& value, std::string& error) override {\n"
                "        if (value.rfind(" + cppString(spec.uriProtocol + "://") + ", 0) != 0) {\n"
                "            error = \"Invalid URI scheme\";\n            return false;\n        }\n"
                "        uri_ = value;\n        return true;\n    }\n"
                "    std::string uri_;\n";
    }
    return text;
}

QString generalOptions(const PluginSpec& spec) {
    const auto primary = std::find_if(spec.ports.begin(), spec.ports.end(), [](const auto& p) {
        return p.direction == PluginDirection::Sink && p.presence == PluginPresence::Always && p.inputMode != PluginInput::Mailbox;
    });
    const bool outputs = spec.hasPort(PluginDirection::Src);
    const bool queued = std::any_of(spec.ports.begin(), spec.ports.end(), [](const auto& p) {
        return p.direction == PluginDirection::Sink && p.inputMode == PluginInput::BoundedQueue;
    });
    QString text = "    void configure() { setSinkRole(" + boolLiteral(!outputs) + "); }\n"
                   "    bool open() override {\n";
    for (const auto& port : spec.ports) {
        const auto name = cppString(port.name);
        if (port.direction == PluginDirection::Src && port.presence == PluginPresence::Sometimes)
            text += "        if (!srcPort(" + name + ")) addSometimesSrcPort(" + name + ", " + caps(port) + ", " + name + ");\n";
        if (port.direction != PluginDirection::Sink || port.presence != PluginPresence::Always) continue;
        if (port.inputMode == PluginInput::Mailbox && !port.mailboxDefault.isEmpty()) {
            const auto value = port.caps() == "application/x-float"
                ? "Buffer::fromPod(static_cast<float>(" + QString::number(port.mailboxDefault.toFloat(), 'g', 9) + "))"
                : "textBuffer(" + cppString(port.mailboxDefault) + ")";
            text += "        sinkPort(" + name + ")->setDefault(" + value + ");\n";
        }
    }
    text += R"cpp(        for (auto* input : allSinkPorts()) {
            if (input->inputMode() != InputMode::Direct) continue;
            input->setDirectCallback([this, input](Buffer buffer) {
                return process(input->name(), std::move(buffer));
            });
        }
)cpp";
    if (primary != spec.ports.end()) {
        text += "        for (auto* src : allSrcPorts()) linkInternal(*sinkPort(" + cppString(primary->name) + "), *src);\n";
    }
    text += "        noMorePads();\n        return true;\n    }\n";
    if (queued) {
        text += R"cpp(    bool start() override {
        for (auto* input : allSinkPorts()) {
            if (input->inputMode() != InputMode::BoundedQueue) continue;
            auto driver = std::make_unique<TaskDriver>();
            auto* worker = driver.get();
            worker->setUnblockCallback([input] { input->unblock(); });
            drivers_.push_back(std::move(driver));
            if (!worker->start(this, [this, input, worker] {
                auto buffer = input->pop();
                if (!buffer) {
                    if (input->isEos() && input->name() == primaryInput()) endStream();
                    worker->pause();
                    return;
                }
                if (process(input->name(), std::move(*buffer)) != GST_FLOW_OK) worker->pause();
            })) {
                stop();
                return false;
            }
        }
        return true;
    }

    void unblock() override {
        for (const auto& driver : drivers_) driver->unblock();
    }

    bool stop() override {
        for (const auto& driver : drivers_) driver->stop();
        drivers_.clear();
        return true;
    }

    std::vector<std::unique_ptr<TaskDriver>> drivers_;
)cpp";
    }
    const QString primaryName = primary == spec.ports.end() ? QString() : primary->name;
    text += "    static constexpr const char* primaryInput() { return " + cppString(primaryName) + "; }\n";
    return text;
}

QString noOptions(const PluginSpec&) { return "    void configure() {}\n"; }
QString sinkOptions(const PluginSpec& spec) {
    return "    void configure() { clockSync().setEnabled(" + boolLiteral(spec.clockSync) + "); }\n";
}

struct ArchetypeBehavior {
    const char* base;
    QString (*options)(const PluginSpec&);
    const char* starter;
};
const ArchetypeBehavior behaviors[]{
    {"VideoFilterElement", noOptions, R"cpp(
    GstFlowReturn filterInPlace(VideoFrame& frame) override {
        (void)frame;  // Add per-frame processing here.
        return GST_FLOW_OK;
    }

    bool onVideoConfigured(const VideoInfo&, const VideoInfo&) override { return isInPlace(); }
)cpp"},
    {"AudioFilterElement", noOptions, R"cpp(
    GstFlowReturn filterInPlace(AudioFrame& frame) override {
        (void)frame;  // Add per-frame processing here.
        return GST_FLOW_OK;
    }

    bool onAudioConfigured(const AudioInfo&, const AudioInfo&) override { return isInPlace(); }
)cpp"},
    {"TransformElement", noOptions, R"cpp(
    GstFlowReturn transform(Buffer& buffer, SrcPort& src) override {
        return src.push(std::move(buffer));
    }
)cpp"},
    {"SourceElement", sourceOptions, R"cpp(
    GstFlowReturn produce(SrcPort& src) override {
        if (index_ == 3) return GST_FLOW_EOS;
        Buffer buffer = Buffer::allocate(bytes_);
        {
            auto map = buffer.write();
            if (!map.valid()) return GST_FLOW_ERROR;
            std::memset(map.data(), 0, map.size());
        }
        buffer.setPts(index_++ * 33 * GST_MSECOND);
        buffer.setDuration(33 * GST_MSECOND);
        return src.push(std::move(buffer));
    }

    uint64_t index_ = 0;
    size_t bytes_ = 16;
)cpp"},
    {"SinkElement", sinkOptions, R"cpp(
    GstFlowReturn consume(Buffer& buffer) override {
        (void)buffer;  // Handle an incoming buffer here.
        return GST_FLOW_OK;
    }
)cpp"},
    {"Element", generalOptions, R"cpp(
    bool onSinkCaps(SinkPort& port, const Caps& input) override {
        if (port.name() != primaryInput()) return true;
        for (auto* src : allSrcPorts()) {
            const auto output = input.intersect(src->templateCaps());
            if (!output.isFixed()) return false;
            src->setCaps(output);
        }
        return true;
    }

    bool onSinkEvent(SinkPort& port, GstEvent* event) override {
        switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_EOS:
            if (port.name() == primaryInput() && port.inputMode() == InputMode::Direct) endStream();
            gst_event_unref(event);
            return true;
        case GST_EVENT_SEGMENT:
            if (port.name() == primaryInput()) {
                GstSegment segment;
                gst_event_copy_segment(event, &segment);
                for (auto* src : allSrcPorts()) src->setSegment(segment);
            }
            gst_event_unref(event);
            return true;
        case GST_EVENT_CAPS:
        case GST_EVENT_STREAM_START:
            gst_event_unref(event);
            return true;
        default:
            return Element::onSinkEvent(port, event);
        }
    }

    GstFlowReturn process(const std::string& input, Buffer buffer) {
        // Read a mailbox input with sinkPort("name")->latest() when you need its latest value.
        if (input != primaryInput()) return GST_FLOW_OK;
        if (isSinkRole()) commitPreroll();
        GstFlowReturn result = GST_FLOW_OK;
        for (auto* src : allSrcPorts()) {
            if (gst_pad_is_linked(src->pad())) result = src->push(buffer);
            if (result != GST_FLOW_OK) return result;
        }
        return result;
    }

    void endStream() {
        if (isSinkRole()) postOrDeferEos();
        for (auto* src : allSrcPorts()) src->pushEos();
    }
)cpp"},
};

const ArchetypeBehavior& behavior(const PluginSpec& spec) {
    return behaviors[static_cast<int>(spec.resolvedArchetype()) - 1];
}

QString region(const QString& prefix, const QString& marker, const QString& body) {
    return prefix + " BEGIN " + marker + "\n" + body + prefix + " END " + marker + "\n";
}

bool replaceRegion(QString& text, const QString& prefix, const QString& marker, const QString& body, QString* error) {
    auto markerLine = [&](const QString& word) {
        return QRegularExpression("^" + QRegularExpression::escape(prefix + " " + word + " " + marker) + "\r?$",
                                  QRegularExpression::MultilineOption).globalMatch(text);
    };
    auto starts = markerLine("BEGIN");
    auto stops = markerLine("END");
    if (!starts.hasNext() || !stops.hasNext()) {
        if (error) *error = "Restore the " + marker + " markers before editing the specification.";
        return false;
    }
    const auto start = starts.next().capturedEnd();
    const auto stop = stops.next().capturedStart();
    if (starts.hasNext() || stops.hasNext() || start >= stop || text.at(start) != '\n') {
        if (error) *error = "Use exactly one ordered pair of " + marker + " markers.";
        return false;
    }
    QString content = body;
    if (start > 0 && text.at(start - 1) == '\r') content.replace("\n", "\r\n");
    text.replace(start + 1, stop - start - 1, content);
    return true;
}

QString dependencies(const PluginSpec& spec) {
    QString out;
    for (const auto& name : spec.extraFindPackages) {
        out += "find_package(" + name + " REQUIRED)\n";
        const QString target = name + "::" + name;
        out += "if(TARGET " + target + ")\n    target_link_libraries(custom_plugin PRIVATE " + target + ")\nendif()\n";
    }
    if (!spec.extraPkgConfig.isEmpty()) {
        out += "pkg_check_modules(PLUGIN_DEPS REQUIRED IMPORTED_TARGET " + spec.extraPkgConfig.join(' ') + ")\n"
               "target_link_libraries(custom_plugin PRIVATE PkgConfig::PLUGIN_DEPS)\n";
    }
    return out;
}

QString optionsAndDescription(const PluginSpec& spec) { return describe(spec) + behavior(spec).options(spec); }

}  // namespace

QString PluginScaffolder::source(const PluginSpec& spec) {
    const auto& type = behavior(spec);
    QString text = R"cpp(#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace gps::elem;

Caps readCaps(const std::string& text) {
    auto caps = Caps::fromString(text);
    if (!caps.isValid() || caps.isEmpty()) throw std::invalid_argument("Invalid caps: " + text);
    return caps;
}

[[maybe_unused]] Buffer textBuffer(const std::string& text) {
    auto buffer = Buffer::allocate(text.size());
    auto map = buffer.write();
    if (map.valid()) std::memcpy(map.data(), text.data(), text.size());
    return buffer;
}

)cpp";
    text += QString("class CustomElement final : public %1 {\npublic:\n"
                    "    explicit CustomElement(GstElement* raw) : %1(raw) { configure(); }\n\n").arg(type.base);
    text += region("//", specMarker, optionsAndDescription(spec));
    text += "\nprivate:\n";
    text += type.starter;
    text += "\n" + region("//", propertiesMarker, properties(spec));
    text += "};\n}  // namespace\n\nGPS_ELEM_DEFINE_PLUGIN(" + spec.id +
            ", CustomElement::pluginDescription, CustomElement::pluginVersion, CustomElement::pluginLicense,\n"
            "    \"GstPipeStudio\", \"https://github.com/mevlt01001/GstPipeStudio\", {\n"
            "    return registerElement<CustomElement>(plugin, " + cppString(spec.id) + ");\n})\n";
    return text;
}

QString PluginScaffolder::cmake(const PluginSpec& spec) {
    return "cmake_minimum_required(VERSION 3.16)\n"
           "project(" + spec.id + " LANGUAGES CXX)\n"
           "find_package(GpsElementSdk CONFIG REQUIRED)\n"
           "add_library(custom_plugin MODULE " + spec.id + ".cpp)\n"
           "set_target_properties(custom_plugin PROPERTIES OUTPUT_NAME gst" + spec.id + " CXX_EXTENSIONS OFF)\n"
           "target_compile_features(custom_plugin PRIVATE cxx_std_17)\n"
           "target_compile_options(custom_plugin PRIVATE -Wall -Wextra)\n"
           "target_link_options(custom_plugin PRIVATE -Wl,--no-undefined)\n"
           "target_link_libraries(custom_plugin PRIVATE GpsElementSdk::Plugin)\n\n" +
           region("#", dependenciesMarker, dependencies(spec));
}

bool PluginScaffolder::updateSpecRegions(const PluginSpec& spec, QString* source, QString* error) {
    QString updated = *source;
    if (!replaceRegion(updated, "//", specMarker, optionsAndDescription(spec), error) ||
        !replaceRegion(updated, "//", propertiesMarker, properties(spec), error)) return false;
    *source = std::move(updated);
    return true;
}

bool PluginScaffolder::updateCMakeRegion(const PluginSpec& spec, QString* cmake, QString* error) {
    return replaceRegion(*cmake, "#", dependenciesMarker, dependencies(spec), error);
}

QString PluginScaffolder::generateAiPrompt(const PluginSpec& spec, const QString& currentSource) {
    return QString::fromUtf8(QJsonDocument(spec.toJson()).toJson(QJsonDocument::Indented)) +
           R"prompt(
Implement the requested algorithm in this C++17 GStreamer element built with gps::elem.
Keep the GPS_PLUGIN_SPEC and GPS_PLUGIN_PROPERTIES regions intact. Use the existing lifecycle hooks.

SDK API excerpts (see the installed gps_elem headers for the full contracts):
- Buffer::allocate(size_t), Buffer::read() const, Buffer::write(), Buffer::ensureWritable()
- Buffer::asPod<T>() const, Buffer::fromPod(const T&), Buffer::pts() const, Buffer::setPts(GstClockTime)
- VideoFrame::mapRead(const VideoInfo&, const Buffer&), VideoFrame::mapWrite(const VideoInfo&, Buffer&)
- AudioFrame::mapRead(const AudioInfo&, const Buffer&), AudioFrame::mapWrite(const AudioInfo&, Buffer&)
- ReadMap::data(), WriteMap::data(), size(), valid()
- SinkPort::latest(), SinkPort::pop(GstClockTime timeoutNs = GST_CLOCK_TIME_NONE)
- SrcPort::setCaps(const Caps&), SrcPort::push(Buffer), SrcPort::pushEos()
- ByteStreamAdapter and CustomMeta<T> are available in gps_elem/byte_stream.h and gps_elem/meta.h.
Check the headers before introducing calls. Keep buffer maps scoped and release them before pushing buffers.

Current source:
```cpp
)prompt" + currentSource + "\n```\n\nRequested algorithm: [DESCRIBE THE ALGORITHM HERE]\n";
}
