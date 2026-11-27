// mcpaudit.h – process-safe persistent audit log for every MCP request and response, with live file watching.
#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

struct McpAuditEntry {
    qint64 seq = 0;
    QString timestamp;
    qint64 pid = 0;
    QString clientName;
    QString clientVersion;
    QString mode;
    QString method;
    QString toolOrResource;
    QString requestJson;
    QString responseJson;
    double durationMs = 0;
    bool ok = true;
    QString errorSummary;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& obj, McpAuditEntry* out);
};

class McpAuditLogger : public QObject {
    Q_OBJECT
public:
    explicit McpAuditLogger(QString dirOverride = {}, QObject* parent = nullptr);

    QString directory() const { return dir_; }
    QString jsonlPath() const;
    QString logPath() const;
    QString socketPath() const;

    qint64 record(McpAuditEntry entry);
    QList<McpAuditEntry> recent(int maxCount = 200) const;
    void clear();

signals:
    void recorded(const McpAuditEntry& entry);
    void cleared();

private:
    void ensureWatched();
    void onFileChanged();

    QString dir_;
    QFileSystemWatcher watcher_;
    qint64 knownOffset_ = 0;
    qint64 lastEmittedSeq_ = 0;
};
