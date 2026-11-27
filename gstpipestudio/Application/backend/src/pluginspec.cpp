#include "pluginspec.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <cstdint>

namespace {

const QStringList archetypes{"Auto", "VideoFilter", "AudioFilter", "Transform", "Source", "Sink", "GeneralElement"};
const QStringList directions{"Sink", "Src"};
const QStringList presences{"Always", "Request", "Sometimes"};
const QStringList capsPresets{"VideoRaw", "VideoNvmm", "AudioRaw", "ByteStream", "CustomMime", "Any"};
const QStringList inputs{"Direct", "Mailbox", "BoundedQueue"};
const QStringList coordinationModes{"Independent", "AllReady", "PtsWindow"};
const QStringList overflows{"Block", "DropOldest", "DropNewest"};
const QStringList propertyTypes{"Bool", "Int", "UInt", "Int64", "UInt64", "Float", "Double", "String"};
const QStringList mutabilities{"Null", "Ready", "Paused", "Playing"};

template <typename Enum>
QString nameOf(const QStringList& names, Enum value) { return names.value(static_cast<int>(value)); }

template <typename Enum>
bool parseEnum(const QJsonObject& object, const char* key, const QStringList& names, Enum& value) {
    if (!object.contains(key)) return true;
    const auto index = names.indexOf(object.value(key).toString());
    if (index < 0) return false;
    value = static_cast<Enum>(index);
    return true;
}

bool matches(const QString& pattern, const QString& text) {
    return QRegularExpression(QRegularExpression::anchoredPattern(pattern)).match(text).hasMatch();
}

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

template <class Number>
QStringList numbers(const PluginPropertySpec& p, const QString& type) {
    auto read = [](const QString& text, Number fallback, bool& ok) -> Number {
        if (text.trimmed().isEmpty()) return fallback;
        bool parsed = false;
        if constexpr (std::is_floating_point_v<Number>) {
            const double value = text.toDouble(&parsed);
            ok = ok && parsed && std::isfinite(value) && std::abs(value) <= std::numeric_limits<Number>::max();
            return ok ? static_cast<Number>(value) : fallback;
        } else if constexpr (std::is_unsigned_v<Number>) {
            const auto value = text.toULongLong(&parsed);
            ok = ok && parsed && value <= std::numeric_limits<Number>::max();
            return ok ? static_cast<Number>(value) : fallback;
        } else {
            const auto value = text.toLongLong(&parsed);
            ok = ok && parsed && value >= std::numeric_limits<Number>::min() && value <= std::numeric_limits<Number>::max();
            return ok ? static_cast<Number>(value) : fallback;
        }
    };
    bool ok = true;
    const Number value = read(p.defaultValue, 0, ok);
    const Number min = read(p.minValue, std::numeric_limits<Number>::lowest(), ok);
    const Number max = read(p.maxValue, std::numeric_limits<Number>::max(), ok);
    if (!ok || min > value || value > max) return {};
    auto literal = [&](Number n) -> QString {
        if constexpr (std::is_floating_point_v<Number>) {
            return "static_cast<" + type + ">(" + QString::number(n, 'g', std::numeric_limits<Number>::max_digits10) + ")";
        } else if constexpr (std::is_unsigned_v<Number>) {
            return type + "{" + QString::number(static_cast<qulonglong>(n)) + "ULL}";
        } else {
            if (n == std::numeric_limits<Number>::min()) return "std::numeric_limits<" + type + ">::min()";
            return type + "{" + QString::number(static_cast<qlonglong>(n)) + "LL}";
        }
    };
    return {literal(value), literal(min), literal(max)};
}

QStringList boolean(const PluginPropertySpec& p, const QString&) {
    const auto value = p.defaultValue.trimmed().toLower();
    if (value == "true" || value == "1") return {"true"};
    if (value.isEmpty() || value == "false" || value == "0") return {"false"};
    return {};
}

struct PropertyBehavior {
    const char* cppType;
    QStringList (*arguments)(const PluginPropertySpec&, const QString&);
};
const PropertyBehavior propertyBehaviors[]{
    {"bool", boolean}, {"int32_t", numbers<int32_t>}, {"uint32_t", numbers<uint32_t>},
    {"int64_t", numbers<int64_t>}, {"uint64_t", numbers<uint64_t>}, {"float", numbers<float>},
    {"double", numbers<double>},
    {"std::string", [](const PluginPropertySpec& p, const QString&) -> QStringList { return {p.defaultValue}; }},
};
}  // namespace

QStringList pluginArchetypeNames() { return archetypes; }
QStringList pluginCapsNames() { return capsPresets; }
QStringList pluginInputNames() { return inputs; }
QStringList pluginCoordinationNames() { return coordinationModes; }
QStringList pluginOverflowNames() { return overflows; }
QStringList pluginPresenceNames() { return presences; }
QStringList pluginPropertyTypeNames() { return propertyTypes; }
QStringList pluginMutabilityNames() { return mutabilities; }

QString PluginPortSpec::caps() const {
    const QStringList base{"video/x-raw", "video/x-raw(memory:NVMM)", "audio/x-raw", "application/octet-stream", "", "ANY"};
    const QString media = nameOf(base, capsPreset);
    const QString detail = capsDetail.trimmed();
    if (capsPreset == PluginCaps::CustomMime) return detail;
    if (capsPreset == PluginCaps::Any || detail.isEmpty()) return media;
    if (detail.startsWith(',')) return media + detail;
    if (capsPreset == PluginCaps::ByteStream) return media;
    return media + ",format=(string){" + detail + "}";
}

QString PluginPropertySpec::cppType() const { return propertyBehaviors[static_cast<int>(type)].cppType; }
QStringList PluginPropertySpec::cppArguments() const { return propertyBehaviors[static_cast<int>(type)].arguments(*this, cppType()); }
QString PluginPropertySpec::valueError() const {
    return cppArguments().isEmpty() ? "Check the default, minimum and maximum for property " + name + "." : QString();
}

bool PluginSpec::hasPort(PluginDirection direction) const {
    return std::any_of(ports.begin(), ports.end(), [direction](const auto& port) { return port.direction == direction; });
}

PluginArchetype PluginSpec::resolvedArchetype() const {
    if (archetype != PluginArchetype::Auto) return archetype;
    const auto sink = std::find_if(ports.begin(), ports.end(), [](const auto& p) { return p.direction == PluginDirection::Sink; });
    const auto src = std::find_if(ports.begin(), ports.end(), [](const auto& p) { return p.direction == PluginDirection::Src; });
    if (std::any_of(ports.begin(), ports.end(), [](const auto& p) {
            return p.presence != PluginPresence::Always || p.inputMode != PluginInput::Direct ||
                   p.name != (p.direction == PluginDirection::Sink ? "sink" : "src");
        })) return PluginArchetype::GeneralElement;
    if (ports.size() == 1) return sink == ports.end() ? PluginArchetype::Source : PluginArchetype::Sink;
    if (ports.size() != 2 || sink == ports.end() || src == ports.end()) return PluginArchetype::GeneralElement;
    if (sink->capsPreset == PluginCaps::VideoRaw && src->capsPreset == PluginCaps::VideoRaw) return PluginArchetype::VideoFilter;
    if (sink->capsPreset == PluginCaps::AudioRaw && src->capsPreset == PluginCaps::AudioRaw) return PluginArchetype::AudioFilter;
    return PluginArchetype::Transform;
}

QString PluginSpec::category() const {
    if (!klass.trimmed().isEmpty()) return klass;
    if (!hasPort(PluginDirection::Sink)) return "Source";
    if (!hasPort(PluginDirection::Src)) return "Sink";
    return "Filter";
}

QString PluginSpec::idError(const QString& id) {
    if (!matches("[a-z][a-z0-9_]*", id))
        return "Use a lowercase factory ID starting with a letter, followed by lowercase letters, digits or underscores.";
    return {};
}

QString PluginSpec::error() const {
    if (const auto why = idError(id); !why.isEmpty()) return why;
    if (longName.trimmed().isEmpty()) return "Enter a display name.";
    if (author.trimmed().isEmpty() || version.trimmed().isEmpty() || license.trimmed().isEmpty())
        return "Enter an author, version and license.";
    if (ports.isEmpty()) return "Add at least one port.";
    QSet<QString> names;
    int sinks = 0;
    for (const auto& port : ports) {
        const QString pattern = port.presence == PluginPresence::Request ? "[a-zA-Z][a-zA-Z0-9_]*(?:%[ud])?" : "[a-zA-Z][a-zA-Z0-9_]*";
        if (!matches(pattern, port.name) || names.contains(port.name)) return "Invalid or duplicate port name: " + port.name;
        names.insert(port.name);
        if (port.caps().isEmpty()) return "Enter caps for port " + port.name + ".";
        if (port.direction == PluginDirection::Sink) {
            ++sinks;
            if (port.presence == PluginPresence::Sometimes) return "Sometimes pads are supported on outputs.";
            if (port.presence == PluginPresence::Request && port.inputMode != PluginInput::Direct)
                return "The generated Request input uses Direct mode.";
            if (port.inputMode == PluginInput::BoundedQueue && port.queueCapacity < 1) return "Queue capacity must be positive.";
            if (port.inputMode == PluginInput::Mailbox && port.caps() == "application/x-float" && !port.mailboxDefault.isEmpty()) {
                bool ok = false;
                const float value = port.mailboxDefault.toFloat(&ok);
                if (!ok || !std::isfinite(value)) return "Enter a finite number for the mailbox default on " + port.name + ".";
            }
        }
    }
    const auto selected = resolvedArchetype();
    if (selected != PluginArchetype::GeneralElement) {
        if (sinks != (selected == PluginArchetype::Source ? 0 : 1) ||
            ports.size() - sinks != (selected == PluginArchetype::Sink ? 0 : 1))
            return "This archetype requires its standard number of input and output ports.";
        for (const auto& p : ports) {
            if (p.name != (p.direction == PluginDirection::Sink ? "sink" : "src") ||
                p.presence != PluginPresence::Always || p.inputMode != PluginInput::Direct)
                return "Use the standard Direct sink and/or src ports, or select GeneralElement.";
            if (selected == PluginArchetype::VideoFilter && p.capsPreset != PluginCaps::VideoRaw)
                return "VideoFilter requires system-memory raw video caps.";
            if (selected == PluginArchetype::AudioFilter && p.capsPreset != PluginCaps::AudioRaw)
                return "AudioFilter requires raw audio caps.";
        }
    }
    if (coordination != PluginCoordination::Independent) {
        const auto queues = std::count_if(ports.begin(), ports.end(), [](const auto& p) {
            return p.direction == PluginDirection::Sink && p.presence == PluginPresence::Always && p.inputMode == PluginInput::BoundedQueue;
        });
        if (selected != PluginArchetype::GeneralElement || queues < 2)
            return "Queue coordination needs at least two Always inputs in BoundedQueue mode.";
        if (toleranceMs < 0) return "The PTS tolerance must be zero or greater.";
    }
    if (!uriProtocol.isEmpty() && (selected != PluginArchetype::Source || !matches("[a-z][a-z0-9+.-]*", uriProtocol)))
        return "Enter a valid URI scheme for a Source archetype.";
    names = {"name", "parent"};
    for (const auto& p : properties) {
        QString canonical = p.name;
        canonical.replace('_', '-');
        if (!matches("[a-zA-Z][a-zA-Z0-9_-]*", p.name) || names.contains(canonical))
            return "Invalid, reserved or duplicate property name: " + p.name;
        names.insert(canonical);
        if (const auto why = p.valueError(); !why.isEmpty()) return why;
    }
    for (const auto& dependency : extraPkgConfig + extraFindPackages) {
        if (!matches("[a-zA-Z0-9_][a-zA-Z0-9_.+-]*", dependency)) return "Invalid dependency name: " + dependency;
    }
    return {};
}

QJsonObject PluginSpec::toJson() const {
    QJsonArray portValues, propertyValues;
    for (const auto& p : ports)
        portValues.append(QJsonObject{{"name", p.name}, {"direction", nameOf(directions, p.direction)},
            {"presence", nameOf(presences, p.presence)}, {"capsPreset", nameOf(capsPresets, p.capsPreset)}, {"capsDetail", p.capsDetail},
            {"inputMode", nameOf(inputs, p.inputMode)}, {"mailboxDefault", p.mailboxDefault}, {"queueCapacity", p.queueCapacity},
            {"overflowPolicy", nameOf(overflows, p.overflowPolicy)}});
    for (const auto& p : properties)
        propertyValues.append(QJsonObject{{"name", p.name}, {"nick", p.nick}, {"blurb", p.blurb},
            {"type", nameOf(propertyTypes, p.type)}, {"default", p.defaultValue}, {"min", p.minValue}, {"max", p.maxValue},
            {"mutability", nameOf(mutabilities, p.mutability)}, {"controllable", p.controllable}});
    return {{"schema", 1}, {"id", id}, {"longName", longName}, {"klass", klass}, {"description", description},
            {"author", author}, {"version", version}, {"license", license}, {"archetype", nameOf(archetypes, archetype)},
            {"live", live}, {"clockSync", clockSync}, {"uriProtocol", uriProtocol},
            {"coordination", nameOf(coordinationModes, coordination)}, {"toleranceMs", toleranceMs},
            {"ports", portValues}, {"properties", propertyValues}, {"extraPkgConfig", QJsonArray::fromStringList(extraPkgConfig)},
            {"extraFindPackages", QJsonArray::fromStringList(extraFindPackages)}};
}

bool PluginSpec::fromJson(const QJsonObject& object, PluginSpec* out, QString* error) {
    if (object.value("schema").toInt() != 1) return fail(error, "Unsupported plugin manifest schema.");
    PluginSpec spec;
    spec.id = object.value("id").toString();
    spec.longName = object.value("longName").toString();
    spec.klass = object.value("klass").toString();
    spec.description = object.value("description").toString();
    spec.author = object.value("author").toString(spec.author);
    spec.version = object.value("version").toString(spec.version);
    spec.license = object.value("license").toString(spec.license);
    spec.live = object.value("live").toBool();
    spec.clockSync = object.value("clockSync").toBool();
    spec.uriProtocol = object.value("uriProtocol").toString();
    spec.toleranceMs = object.value("toleranceMs").toInt(spec.toleranceMs);
    if (!parseEnum(object, "coordination", coordinationModes, spec.coordination)) return fail(error, "Unknown queue coordination mode.");
    if (!parseEnum(object, "archetype", archetypes, spec.archetype)) return fail(error, "Unknown plugin archetype.");
    spec.ports.clear();
    for (const auto& value : object.value("ports").toArray()) {
        const auto row = value.toObject();
        PluginPortSpec p;
        p.name = row.value("name").toString();
        p.capsDetail = row.value("capsDetail").toString();
        p.mailboxDefault = row.value("mailboxDefault").toString();
        p.queueCapacity = row.value("queueCapacity").toInt(p.queueCapacity);
        if (!parseEnum(row, "direction", directions, p.direction) || !parseEnum(row, "presence", presences, p.presence) ||
            !parseEnum(row, "capsPreset", capsPresets, p.capsPreset) || !parseEnum(row, "inputMode", inputs, p.inputMode) ||
            !parseEnum(row, "overflowPolicy", overflows, p.overflowPolicy)) return fail(error, "Unknown port option.");
        spec.ports << p;
    }
    for (const auto& value : object.value("properties").toArray()) {
        const auto row = value.toObject();
        PluginPropertySpec p;
        p.name = row.value("name").toString();
        p.nick = row.value("nick").toString();
        p.blurb = row.value("blurb").toString();
        p.defaultValue = row.value("default").toString();
        p.minValue = row.value("min").toString();
        p.maxValue = row.value("max").toString();
        p.controllable = row.value("controllable").toBool();
        if (!parseEnum(row, "type", propertyTypes, p.type) || !parseEnum(row, "mutability", mutabilities, p.mutability))
            return fail(error, "Unknown property option.");
        spec.properties << p;
    }
    for (const auto& item : object.value("extraPkgConfig").toArray()) spec.extraPkgConfig << item.toString();
    for (const auto& item : object.value("extraFindPackages").toArray()) spec.extraFindPackages << item.toString();
    if (const auto why = spec.error(); !why.isEmpty()) return fail(error, why);
    *out = std::move(spec);
    return true;
}
