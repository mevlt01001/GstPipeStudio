// updateresult.h – what gps-update reports (SPEC 7), plus the two failures only the GUI can see.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "elementinfo.h"
#include "runtime.h"

struct PadReport {
    QString element, pad, identity, templateName, presence, caps, allowed;
    bool isSrc = true, linked = false, temporaryEnd = false, unknownData = false;
    QList<StructureInfo> allowedStructures;
};

struct UpdateResult {
    // ok | error | timeout | pad-missing come from gps-update; crashed | stopped | hung are decided by the GUI.
    QString status;
    QString message, errorElement;
    double ms = 0;
    QList<PadReport> pads;
    QStringList missing, refused;
    bool complete = false;

    bool valid() const { return !status.isEmpty(); }  // false before the first update
    bool ok() const { return status == "ok"; }
    bool isComplete() const { return ok() && complete; }
    gps::UpdateResult toCore() const;
    static UpdateResult fromCore(const gps::UpdateResult&);
    QString statusText() const;  // for the panel and the log

    static UpdateResult fromJson(const QJsonObject& json);
    static UpdateResult failure(const QString& status, const QString& message, const QString& element);
    const PadReport* pad(const QString& element, const QString& identity, bool isSrc) const;
};
