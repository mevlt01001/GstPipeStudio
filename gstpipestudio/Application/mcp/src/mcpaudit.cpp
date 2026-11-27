// mcpaudit.cpp – see mcpaudit.h.
#include "mcpaudit.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>

#include "apppaths.h"

namespace {

QString resolveAuditDir(const QString& dirOverride) {
    if (!dirOverride.isEmpty()) return dirOverride;
    const QString env = qEnvironmentVariable("GPS_MCP_AUDIT_DIR");
    if (!env.isEmpty()) return env;
    return QDir(AppPaths::state()).filePath("mcp");
}

QString formatHumanLine(const McpAuditEntry& e) {
    QString line = QString("[%1] #%2 pid=%3 mode=%4 client=%5/%6 method=%7")
                       .arg(e.timestamp)
                       .arg(e.seq)
                       .arg(e.pid)
                       .arg(e.mode.isEmpty() ? QStringLiteral("headless") : e.mode)
                       .arg(e.clientName.isEmpty() ? QStringLiteral("unknown") : e.clientName)
                       .arg(e.clientVersion.isEmpty() ? QStringLiteral("-") : e.clientVersion)
                       .arg(e.method);
    if (!e.toolOrResource.isEmpty()) line += QString(" target=%1").arg(e.toolOrResource);
    line += QString(" ok=%1 (%2 ms)").arg(e.ok ? QStringLiteral("true") : QStringLiteral("false")).arg(e.durationMs, 0, 'f', 2);
    if (!e.errorSummary.isEmpty()) line += QString(" error=%1").arg(e.errorSummary);
    return line + '\n';
}

}  // namespace

QJsonObject McpAuditEntry::toJson() const {
    return {
        {"seq", seq},
        {"timestamp", timestamp},
        {"pid", pid},
        {"clientName", clientName},
        {"clientVersion", clientVersion},
        {"mode", mode},
        {"method", method},
        {"toolOrResource", toolOrResource},
        {"requestJson", requestJson},
        {"responseJson", responseJson},
        {"durationMs", durationMs},
        {"ok", ok},
        {"errorSummary", errorSummary},
    };
}

bool McpAuditEntry::fromJson(const QJsonObject& obj, McpAuditEntry* out) {
    if (!out || obj.isEmpty()) return false;
    McpAuditEntry e;
    e.seq = static_cast<qint64>(obj.value("seq").toDouble(0));
    e.timestamp = obj.value("timestamp").toString();
    e.pid = static_cast<qint64>(obj.value("pid").toDouble(0));
    e.clientName = obj.value("clientName").toString();
    e.clientVersion = obj.value("clientVersion").toString();
    e.mode = obj.value("mode").toString();
    e.method = obj.value("method").toString();
    e.toolOrResource = obj.value("toolOrResource").toString();
    e.requestJson = obj.value("requestJson").toString();
    e.responseJson = obj.value("responseJson").toString();
    e.durationMs = obj.value("durationMs").toDouble(0.0);
    e.ok = obj.value("ok").toBool(true);
    e.errorSummary = obj.value("errorSummary").toString();
    *out = std::move(e);
    return true;
}

McpAuditLogger::McpAuditLogger(QString dirOverride, QObject* parent)
    : QObject(parent), dir_(resolveAuditDir(dirOverride)) {
    QDir().mkpath(dir_);
    QFile f(jsonlPath());
    if (f.exists()) {
        knownOffset_ = f.size();
        const auto entries = recent(1);
        if (!entries.isEmpty()) lastEmittedSeq_ = entries.last().seq;
    }
    ensureWatched();
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &McpAuditLogger::onFileChanged);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
        ensureWatched();
        onFileChanged();
    });
}

QString McpAuditLogger::jsonlPath() const { return QDir(dir_).filePath("mcp_audit.jsonl"); }
QString McpAuditLogger::logPath() const { return QDir(dir_).filePath("mcp_audit.log"); }
QString McpAuditLogger::socketPath() const { return QDir(dir_).filePath("live.sock"); }

void McpAuditLogger::ensureWatched() {
    if (!dir_.isEmpty() && !watcher_.directories().contains(dir_)) watcher_.addPath(dir_);
    const QString path = jsonlPath();
    if (QFileInfo::exists(path) && !watcher_.files().contains(path)) watcher_.addPath(path);
}

qint64 McpAuditLogger::record(McpAuditEntry entry) {
    QDir().mkpath(dir_);
    QLockFile lock(QDir(dir_).filePath("mcp_audit.lock"));
    lock.setStaleLockTime(5000);
    lock.lock();

    if (entry.timestamp.isEmpty()) entry.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (entry.pid == 0) entry.pid = QCoreApplication::applicationPid();

    QFile jsonl(jsonlPath());
    qint64 nextSeq = 1;
    if (jsonl.open(QIODevice::ReadOnly)) {
        while (!jsonl.atEnd()) {
            const QByteArray line = jsonl.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonObject obj = QJsonDocument::fromJson(line).object();
            const qint64 s = static_cast<qint64>(obj.value("seq").toDouble(0));
            if (s >= nextSeq) nextSeq = s + 1;
        }
        jsonl.close();
    }
    if (entry.seq <= 0) entry.seq = nextSeq;

    if (jsonl.open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QByteArray line = QJsonDocument(entry.toJson()).toJson(QJsonDocument::Compact) + '\n';
        jsonl.write(line);
        jsonl.flush();
        knownOffset_ = jsonl.size();
        jsonl.close();
    }

    QFile human(logPath());
    if (human.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        human.write(formatHumanLine(entry).toUtf8());
        human.flush();
        human.close();
    }

    lock.unlock();
    ensureWatched();
    if (entry.seq > lastEmittedSeq_) {
        lastEmittedSeq_ = entry.seq;
        emit recorded(entry);
    }
    return entry.seq;
}

QList<McpAuditEntry> McpAuditLogger::recent(int maxCount) const {
    QList<McpAuditEntry> out;
    if (maxCount <= 0) return out;

    QFile jsonl(jsonlPath());
    if (!jsonl.open(QIODevice::ReadOnly)) return out;

    while (!jsonl.atEnd()) {
        const QByteArray line = jsonl.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        McpAuditEntry entry;
        if (McpAuditEntry::fromJson(doc.object(), &entry)) {
            out.append(std::move(entry));
            if (out.size() > maxCount) out.removeFirst();
        }
    }
    return out;
}

void McpAuditLogger::clear() {
    QDir().mkpath(dir_);
    QLockFile lock(QDir(dir_).filePath("mcp_audit.lock"));
    lock.setStaleLockTime(5000);
    lock.lock();

    QFile jsonl(jsonlPath());
    jsonl.open(QIODevice::WriteOnly | QIODevice::Truncate);
    jsonl.close();

    QFile human(logPath());
    human.open(QIODevice::WriteOnly | QIODevice::Truncate);
    human.close();

    knownOffset_ = 0;
    lastEmittedSeq_ = 0;
    lock.unlock();
    emit cleared();
}

void McpAuditLogger::onFileChanged() {
    ensureWatched();
    QFile jsonl(jsonlPath());
    if (!jsonl.exists()) {
        if (knownOffset_ != 0) {
            knownOffset_ = 0;
            lastEmittedSeq_ = 0;
            emit cleared();
        }
        return;
    }
    const qint64 sz = jsonl.size();
    if (sz < knownOffset_) {
        knownOffset_ = 0;
        lastEmittedSeq_ = 0;
        emit cleared();
    }
    if (sz == knownOffset_) return;
    if (!jsonl.open(QIODevice::ReadOnly)) return;
    if (!jsonl.seek(knownOffset_)) {
        knownOffset_ = 0;
        jsonl.seek(0);
    }
    while (!jsonl.atEnd()) {
        const QByteArray line = jsonl.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        McpAuditEntry entry;
        if (McpAuditEntry::fromJson(doc.object(), &entry) && entry.seq > lastEmittedSeq_) {
            lastEmittedSeq_ = entry.seq;
            emit recorded(entry);
        }
    }
    knownOffset_ = jsonl.pos();
}
