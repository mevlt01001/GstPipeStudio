// updateresult.cpp – gps-update JSON to data.
#include "updateresult.h"

#include <QJsonArray>

namespace {

QStringList strings(const QJsonValue& v) {
    QStringList out;
    for (const QJsonValue& item : v.toArray()) out << item.toString();
    return out;
}

}  // namespace

QString UpdateResult::statusText() const {
    if (status.isEmpty()) return "Not validated yet";
    if (status == "ok") return complete ? "Valid, pipeline complete" : "Valid, pipeline not complete yet";
    if (status == "error") return "Problem";
    if (status == "timeout") return "Timed out";
    if (status == "pad-missing") return "Pad did not appear";
    if (status == "crashed") return "Validator stopped unexpectedly";
    if (status == "stopped") return "Validator gave no answer";
    if (status == "hung") return "Validator did not answer";
    return status;
}

UpdateResult UpdateResult::fromJson(const QJsonObject& json) {
    UpdateResult r;
    r.status = json["status"].toString();
    r.message = json["message"].toString();
    r.errorElement = json["errorElement"].toString();
    r.ms = json["ms"].toDouble();
    r.complete = json["complete"].toBool();
    r.missing = strings(json["missing"]);
    r.refused = strings(json["refused"]);
    for (const QJsonValue& v : json["pads"].toArray()) {
        QJsonObject o = v.toObject();
        PadReport p;
        p.element = o["element"].toString();
        p.pad = o["pad"].toString();
        p.identity = o["identity"].toString();
        p.templateName = o["template"].toString();
        p.presence = o["presence"].toString();
        p.caps = o["caps"].toString();
        p.allowed = o["allowed"].toString();
        p.isSrc = o["isSrc"].toBool();
        p.linked = o["linked"].toBool();
        p.temporaryEnd = o["temporaryEnd"].toBool();
        p.unknownData = o["unknownData"].toBool();
        for (const QJsonValue& s : o["allowedStructures"].toArray()) p.allowedStructures << StructureInfo::fromJson(s.toObject());
        r.pads << p;
    }
    return r;
}

UpdateResult UpdateResult::failure(const QString& status, const QString& message, const QString& element) {
    UpdateResult r;
    r.status = status;
    r.message = message;
    r.errorElement = element;
    return r;
}

const PadReport* UpdateResult::pad(const QString& element, const QString& identity, bool isSrc) const {
    for (const PadReport& p : pads) {
        if (p.element == element && p.identity == identity && p.isSrc == isSrc) return &p;
    }
    return nullptr;
}

gps::UpdateResult UpdateResult::toCore() const {
    const auto& source = *this;
    gps::UpdateResult out;
    out.status = toStd(source.status);
    out.message = toStd(source.message);
    out.errorElement = toStd(source.errorElement);
    out.ms = source.ms;
    for (const auto& value : source.missing) out.missing.push_back(toStd(value));
    for (const auto& value : source.refused) out.refused.push_back(toStd(value));
    for (const auto& p : source.pads) {
        gps::PadReport pad;
        pad.element = toStd(p.element);
        pad.pad = toStd(p.pad);
        pad.identity = toStd(p.identity);
        pad.templateName = toStd(p.templateName);
        pad.presence = toStd(p.presence);
        pad.caps = toStd(p.caps);
        pad.allowed = toStd(p.allowed);
        pad.isSrc = p.isSrc;
        pad.linked = p.linked;
        pad.temporaryEnd = p.temporaryEnd;
        pad.unknownData = p.unknownData;
        for (const auto& st : p.allowedStructures) {
            gps::StructureInfo structure;
            structure.media = toStd(st.media);
            for (const auto& feature : st.features) structure.features.push_back(toStd(feature));
            for (const auto& f : st.fields) {
                gps::CapsFieldInfo field;
                field.name = toStd(f.name);
                field.type = toStd(f.type);
                field.kind = toStd(f.kind);
                for (const auto& value : f.values) field.values.push_back(toStd(value));
                structure.fields.push_back(std::move(field));
            }
            pad.allowedStructures.push_back(std::move(structure));
        }
        out.pads.push_back(std::move(pad));
    }
    return out;
}

UpdateResult UpdateResult::fromCore(const gps::UpdateResult& source) {
    UpdateResult out;
    out.status = fromStd(source.status);
    out.message = fromStd(source.message);
    out.errorElement = fromStd(source.errorElement);
    out.ms = source.ms;
    out.complete = source.complete();
    for (const auto& value : source.missing) out.missing.push_back(fromStd(value));
    for (const auto& value : source.refused) out.refused.push_back(fromStd(value));
    for (const auto& p : source.pads) {
        PadReport pad;
        pad.element = fromStd(p.element);
        pad.pad = fromStd(p.pad);
        pad.identity = fromStd(p.identity);
        pad.templateName = fromStd(p.templateName);
        pad.presence = fromStd(p.presence);
        pad.caps = fromStd(p.caps);
        pad.allowed = fromStd(p.allowed);
        pad.isSrc = p.isSrc;
        pad.linked = p.linked;
        pad.temporaryEnd = p.temporaryEnd;
        pad.unknownData = p.unknownData;
        for (const auto& st : p.allowedStructures) {
            StructureInfo structure;
            structure.media = fromStd(st.media);
            for (const auto& feature : st.features) structure.features.push_back(fromStd(feature));
            for (const auto& f : st.fields) {
                FieldInfo field;
                field.name = fromStd(f.name);
                field.type = fromStd(f.type);
                field.kind = fromStd(f.kind);
                for (const auto& value : f.values) field.values.push_back(fromStd(value));
                structure.fields.push_back(std::move(field));
            }
            pad.allowedStructures.push_back(std::move(structure));
        }
        out.pads.push_back(std::move(pad));
    }
    return out;
}
