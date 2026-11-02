// elementinfo.h – what gps-inspect tells about an element (SPEC 7), as plain Qt data.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct FieldInfo {
    QString name;
    QString type;  // GType name of the value (of one item for list and range)
    QString kind;  // fixed | list | range
    QStringList values;
};

struct StructureInfo {
    QString media;  // video/x-raw
    QStringList features;
    QList<FieldInfo> fields;

    QString title() const;  // "video/x-raw(memory:NVMM)"
    static StructureInfo fromJson(const QJsonObject& json);
};

struct EnumValue {
    QString nick, name, value;
};

struct PropertyInfo {
    QString name, nick, blurb, kind, typeName, defaultValue, min, max;
    QList<EnumValue> enumValues;
    bool readable = false, writable = false, construct = false, constructOnly = false, deprecated = false;
    bool mutableReady = false, mutablePaused = false, mutablePlaying = false;

    static PropertyInfo fromJson(const QJsonObject& json);
};

struct PadTemplateInfo {
    QString name, direction, presence, caps;
    QList<StructureInfo> structures;
    QList<PropertyInfo> padProperties;

    bool isSrc() const { return direction == "src"; }
    bool isRequest() const { return presence == "request"; }
};

struct ElementInfo {
    QString factory, longName, klass, description, author, plugin;
    int rank = 0;
    bool isSource = false, isSink = false;
    QList<PadTemplateInfo> padTemplates;
    QList<PropertyInfo> properties;

    static ElementInfo fromJson(const QJsonObject& json);
    QList<PadTemplateInfo> sinkTemplates() const;
    QList<PadTemplateInfo> srcTemplates() const;
    const PadTemplateInfo* padTemplate(const QString& name, const QString& direction) const;
    // Sink templates whose caps meet the given structures; all of them when none does (the user decides).
    QList<PadTemplateInfo> sinkTemplatesFor(const QList<StructureInfo>& allowed) const;
};

// One row of "gps-inspect list".
struct ListEntry {
    QString factory, klass, longName, plugin;
    int rank = 0;
    bool isSource = false, isSink = false, hasRequestSrc = false, nvidia = false;

    static ListEntry fromJson(const QJsonObject& json);
};

// Pad caps meet by media type and memory features. An empty side (ANY) meets everything.
bool capsCompatible(const QList<StructureInfo>& a, const QList<StructureInfo>& b);
// "video/x-raw(memory:NVMM), width=(int)1" -> "video/x-raw(memory:NVMM)"
QString mediaOf(const QString& caps);
