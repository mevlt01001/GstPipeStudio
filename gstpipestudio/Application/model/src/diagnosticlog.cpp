// diagnosticlog.cpp
#include "diagnosticlog.h"

#include <QTime>

namespace {
QString channelTag(LogChannel channel) {
    switch (channel) {
    case LogChannel::Document: return "doc";
    case LogChannel::PluginBuild: return "plugin";
    case LogChannel::Engine: return "engine";
    case LogChannel::Mcp: return "mcp";
    case LogChannel::System: return "system";
    }
    return "system";
}
}  // namespace

QString LogRecord::formatted() const {
    const QString time = QDateTime::fromMSecsSinceEpoch(timestampMs).time().toString("HH:mm:ss");
    const QString tag = source.isEmpty() ? channelTag(channel) : channelTag(channel) + ":" + source;
    return QString("%1  [%2] %3").arg(time, tag, message);
}

void DiagnosticLog::append(LogChannel channel, NoticeSeverity severity, const QString& source, const QString& message) {
    LogRecord rec{QDateTime::currentMSecsSinceEpoch(), channel, severity, source, message};
    records_.append(rec);
    while (records_.size() > kMaxRecords) records_.removeFirst();
    emit recorded(rec);
}

QList<LogRecord> DiagnosticLog::records(LogChannel channel) const {
    QList<LogRecord> out;
    for (const LogRecord& r : records_) {
        if (r.channel == channel) out.append(r);
    }
    return out;
}

void DiagnosticLog::clear() {
    records_.clear();
    emit cleared();
}
