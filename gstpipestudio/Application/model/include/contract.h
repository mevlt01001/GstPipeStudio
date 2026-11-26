// contract.h – the Pipeline contract types as the GUI names them, and the conversions between std and Qt text.
// The structs themselves live in Pipeline (catalog.h, runtime.h); the GUI keeps no copy of them.
#pragma once

#include <QString>
#include <QStringList>
#include <string>
#include <vector>

#include "catalog.h"
#include "runtime.h"

using gps::CapsFieldInfo;
using gps::ElementInfo;
using gps::EnumValue;
using gps::FactoryEntry;
using gps::PadReport;
using gps::PadTemplateInfo;
using gps::PropertyInfo;
using gps::StructureInfo;
using gps::UpdateResult;

inline std::string toStd(const QString& s) { return s.toStdString(); }
inline QString fromStd(const std::string& s) { return QString::fromStdString(s); }
QStringList fromStd(const std::vector<std::string>& list);
std::vector<std::string> toStd(const QStringList& list);
// list[i] as Qt text, or fallback when the list is shorter.
QString itemAt(const std::vector<std::string>& list, size_t i, const QString& fallback = QString());

// "Valid, pipeline complete", "Problem" ...: the validation status for the panel and the log.
QString statusText(const UpdateResult& result);
