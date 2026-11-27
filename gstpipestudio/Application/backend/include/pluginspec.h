#pragma once

#include <QJsonObject>
#include <QList>
#include <QStringList>

enum class PluginArchetype { Auto, VideoFilter, AudioFilter, Transform, Source, Sink, GeneralElement };
enum class PluginDirection { Sink, Src };
enum class PluginPresence { Always, Request, Sometimes };
enum class PluginCaps { VideoRaw, VideoNvmm, AudioRaw, ByteStream, CustomMime, Any };
enum class PluginInput { Direct, Mailbox, BoundedQueue };
enum class PluginCoordination { Independent, AllReady, PtsWindow };
enum class PluginOverflow { Block, DropOldest, DropNewest };
enum class PluginPropertyType { Bool, Int, UInt, Int64, UInt64, Float, Double, String };
enum class PluginMutability { Null, Ready, Paused, Playing };

struct PluginPortSpec {
    QString name;
    PluginDirection direction = PluginDirection::Sink;
    PluginPresence presence = PluginPresence::Always;
    PluginCaps capsPreset = PluginCaps::VideoRaw;
    QString capsDetail;
    PluginInput inputMode = PluginInput::Direct;
    QString mailboxDefault{};
    int queueCapacity = 8;
    PluginOverflow overflowPolicy = PluginOverflow::Block;

    QString caps() const;
};

struct PluginPropertySpec {
    QString name, nick, blurb;
    PluginPropertyType type = PluginPropertyType::Int;
    QString defaultValue, minValue, maxValue;
    PluginMutability mutability = PluginMutability::Playing;
    bool controllable = false;

    QString cppType() const;
    QString valueError() const;
    QStringList cppArguments() const;
};

struct PluginSpec {
    QString id, longName, description;
    QString klass;
    QString author = "GstPipeStudio";
    QString version = "0.1.0";
    QString license = "LGPL";
    PluginArchetype archetype = PluginArchetype::Auto;
    QList<PluginPortSpec> ports{{"sink", PluginDirection::Sink, PluginPresence::Always, PluginCaps::VideoRaw, "RGBA"},
                                {"src", PluginDirection::Src, PluginPresence::Always, PluginCaps::VideoRaw, "RGBA"}};
    QList<PluginPropertySpec> properties;
    QStringList extraPkgConfig, extraFindPackages;
    PluginCoordination coordination = PluginCoordination::Independent;
    int toleranceMs = 20;
    bool live = false;
    bool clockSync = false;
    QString uriProtocol;

    PluginArchetype resolvedArchetype() const;
    bool hasPort(PluginDirection direction) const;
    QString category() const;
    QString error() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& object, PluginSpec* spec, QString* error);
    static QString idError(const QString& id);
};

QStringList pluginArchetypeNames();
QStringList pluginCapsNames();
QStringList pluginInputNames();
QStringList pluginOverflowNames();
QStringList pluginCoordinationNames();
QStringList pluginPresenceNames();
QStringList pluginPropertyTypeNames();
QStringList pluginMutabilityNames();
