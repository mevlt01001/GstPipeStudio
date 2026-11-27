// diagnosticlog.h – structured application-wide diagnostic log bus segmented by channel.
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include "notice.h"

enum class LogChannel {
    Document,
    PluginBuild,
    Engine,
    Mcp,
    System,
};

struct LogRecord {
    qint64 timestampMs = 0;
    LogChannel channel = LogChannel::System;
    NoticeSeverity severity = NoticeSeverity::Info;
    QString source;
    QString message;

    QString formatted() const;
};

class DiagnosticLog : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxRecords = 5000;

    explicit DiagnosticLog(QObject* parent = nullptr) : QObject(parent) {}

    void append(LogChannel channel, NoticeSeverity severity, const QString& source, const QString& message);
    void info(LogChannel channel, const QString& source, const QString& message) {
        append(channel, NoticeSeverity::Info, source, message);
    }
    void warn(LogChannel channel, const QString& source, const QString& message) {
        append(channel, NoticeSeverity::Warning, source, message);
    }
    void error(LogChannel channel, const QString& source, const QString& message) {
        append(channel, NoticeSeverity::Error, source, message);
    }

    QList<LogRecord> records() const { return records_; }
    QList<LogRecord> records(LogChannel channel) const;
    void clear();

signals:
    void recorded(const LogRecord& record);
    void cleared();

private:
    QList<LogRecord> records_;
};
