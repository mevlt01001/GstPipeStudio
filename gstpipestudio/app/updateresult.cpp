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
    if (status.isEmpty()) return "Güncelleme yapılmadı";
    if (status == "ok") return complete ? "Tamam, pipeline tamamlandı" : "Tamam, pipeline henüz tamamlanmadı";
    if (status == "error") return "Hata";
    if (status == "timeout") return "Zaman aşımı";
    if (status == "pad-missing") return "Pad oluşmadı";
    if (status == "crashed") return "Yardımcı çöktü";
    if (status == "hung") return "Yardımcı takıldı";
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
