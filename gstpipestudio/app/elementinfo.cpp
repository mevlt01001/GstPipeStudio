// elementinfo.cpp – JSON to plain data. Numbers come as text (SPEC 7) but are read from either form.
#include "elementinfo.h"

#include <QJsonArray>
#include <algorithm>

namespace {

QString textOf(const QJsonValue& v) {
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? "true" : "false";
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 17);
    return QString();
}

int intOf(const QJsonValue& v) { return v.isString() ? v.toString().toInt() : v.toInt(); }

QStringList stringList(const QJsonValue& v) {
    QStringList out;
    for (const QJsonValue& item : v.toArray()) out << textOf(item);
    return out;
}

template <typename T>
QList<T> listOf(const QJsonValue& v) {
    QList<T> out;
    for (const QJsonValue& item : v.toArray()) out << T::fromJson(item.toObject());
    return out;
}

}  // namespace

QString StructureInfo::title() const {
    return features.isEmpty() ? media : media + "(" + features.join(", ") + ")";
}

StructureInfo StructureInfo::fromJson(const QJsonObject& json) {
    StructureInfo s;
    s.media = json["media"].toString();
    s.features = stringList(json["features"]);
    for (const QJsonValue& f : json["fields"].toArray()) {
        QJsonObject o = f.toObject();
        s.fields.append({o["name"].toString(), o["type"].toString(), o["kind"].toString(), stringList(o["values"])});
    }
    return s;
}

PropertyInfo PropertyInfo::fromJson(const QJsonObject& json) {
    PropertyInfo p;
    p.name = json["name"].toString();
    p.nick = json["nick"].toString();
    p.blurb = json["blurb"].toString();
    p.kind = json["kind"].toString();
    p.typeName = json["typeName"].toString();
    p.defaultValue = textOf(json["default"]);
    p.min = textOf(json["min"]);
    p.max = textOf(json["max"]);
    for (const QJsonValue& v : json["enumValues"].toArray()) {
        QJsonObject o = v.toObject();
        p.enumValues.append({o["nick"].toString(), o["name"].toString(), textOf(o["value"])});
    }
    p.readable = json["readable"].toBool();
    p.writable = json["writable"].toBool();
    p.construct = json["construct"].toBool();
    p.constructOnly = json["constructOnly"].toBool();
    p.deprecated = json["deprecated"].toBool();
    p.mutableReady = json["mutableReady"].toBool();
    p.mutablePaused = json["mutablePaused"].toBool();
    p.mutablePlaying = json["mutablePlaying"].toBool();
    return p;
}

ElementInfo ElementInfo::fromJson(const QJsonObject& json) {
    ElementInfo e;
    e.factory = json["factory"].toString();
    e.longName = json["longName"].toString();
    e.klass = json["klass"].toString();
    e.description = json["description"].toString();
    e.author = json["author"].toString();
    e.plugin = json["plugin"].toString();
    e.rank = intOf(json["rank"]);
    e.isSource = json["isSource"].toBool();
    e.isSink = json["isSink"].toBool();
    for (const QJsonValue& v : json["padTemplates"].toArray()) {
        QJsonObject o = v.toObject();
        PadTemplateInfo t;
        t.name = o["name"].toString();
        t.direction = o["direction"].toString();
        t.presence = o["presence"].toString();
        t.caps = o["caps"].toString();
        t.structures = listOf<StructureInfo>(o["structures"]);
        t.padProperties = listOf<PropertyInfo>(o["padProperties"]);
        e.padTemplates.append(t);
    }
    e.properties = listOf<PropertyInfo>(json["properties"]);
    return e;
}

QList<PadTemplateInfo> ElementInfo::sinkTemplates() const {
    QList<PadTemplateInfo> out;
    for (const PadTemplateInfo& t : padTemplates) {
        if (!t.isSrc()) out.append(t);
    }
    return out;
}

QList<PadTemplateInfo> ElementInfo::srcTemplates() const {
    QList<PadTemplateInfo> out;
    for (const PadTemplateInfo& t : padTemplates) {
        if (t.isSrc()) out.append(t);
    }
    return out;
}

const PadTemplateInfo* ElementInfo::padTemplate(const QString& name, const QString& direction) const {
    for (const PadTemplateInfo& t : padTemplates) {
        if (t.name == name && t.direction == direction) return &t;
    }
    return nullptr;
}

QList<PadTemplateInfo> ElementInfo::sinkTemplatesFor(const QList<StructureInfo>& allowed) const {
    QList<PadTemplateInfo> all = sinkTemplates();
    QList<PadTemplateInfo> fitting;
    for (const PadTemplateInfo& t : all) {
        if (capsCompatible(allowed, t.structures)) fitting.append(t);
    }
    return fitting.isEmpty() ? all : fitting;
}

ListEntry ListEntry::fromJson(const QJsonObject& json) {
    ListEntry e;
    e.factory = json["factory"].toString();
    e.klass = json["klass"].toString();
    e.longName = json["longName"].toString();
    e.plugin = json["plugin"].toString();
    e.rank = intOf(json["rank"]);
    e.isSource = json["isSource"].toBool();
    e.isSink = json["isSink"].toBool();
    e.hasRequestSrc = json["hasRequestSrc"].toBool();
    e.nvidia = json["nvidia"].toBool();
    return e;
}

bool capsCompatible(const QList<StructureInfo>& a, const QList<StructureInfo>& b) {
    if (a.isEmpty() || b.isEmpty()) return true;
    for (const StructureInfo& x : a) {
        for (const StructureInfo& y : b) {
            QStringList fx = x.features, fy = y.features;
            std::sort(fx.begin(), fx.end());
            std::sort(fy.begin(), fy.end());
            if (x.media == y.media && fx == fy) return true;
        }
    }
    return false;
}

QString mediaOf(const QString& caps) {
    int comma = caps.indexOf(',');
    return (comma < 0 ? caps : caps.left(comma)).trimmed();
}
