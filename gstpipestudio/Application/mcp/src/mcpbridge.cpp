// mcpbridge.cpp – see mcpbridge.h.
#include "mcpbridge.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>

#include "appservices.h"
#include "controller.h"
#include "diagnosticlog.h"
#include "mcpservercore.h"

McpBridgeServer::McpBridgeServer(AppServices& services, McpAuditLogger& audit, ControllerProvider frontController,
                                 QObject* parent)
    : QObject(parent), services_(services), audit_(audit), frontController_(std::move(frontController)),
      core_(new McpServerCore(services, audit, /*headless=*/true, this)) {
    connect(&server_, &QLocalServer::newConnection, this, &McpBridgeServer::onNewConnection);
}

McpBridgeServer::~McpBridgeServer() { stop(); }

bool McpBridgeServer::start() {
    if (server_.isListening()) return true;
    const QString path = socketPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLocalServer::removeServer(path);
    if (!server_.listen(path)) {
        services_.diagnosticLog().warn(LogChannel::Mcp, "McpBridgeServer",
                                       QString("Failed to listen on %1: %2").arg(path, server_.errorString()));
        return false;
    }
    return true;
}

void McpBridgeServer::stop() {
    if (server_.isListening()) server_.close();
    QLocalServer::removeServer(socketPath());
}

void McpBridgeServer::onNewConnection() {
    while (QLocalSocket* socket = server_.nextPendingConnection()) {
        auto* buffer = new QByteArray();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            while (true) {
                const int nl = buffer->indexOf('\n');
                if (nl < 0) break;
                const QByteArray line = buffer->left(nl).trimmed();
                buffer->remove(0, nl + 1);
                if (!line.isEmpty()) handleSocketLine(socket, line);
            }
        });
        connect(socket, &QLocalSocket::disconnected, socket, [socket, buffer] {
            delete buffer;
            socket->deleteLater();
        });
    }
}

void McpBridgeServer::handleSocketLine(QLocalSocket* socket, const QByteArray& line) {
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        const QJsonObject errReply{{"ok", false}, {"error", QString("Invalid bridge JSON: %1").arg(parseErr.errorString())}};
        socket->write(QJsonDocument(errReply).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
        return;
    }

    const QJsonObject req = doc.object();
    const QString method = req.value("method").toString();
    const QJsonObject params = req.value("params").toObject();
    const QString target = method == "tools/call" ? params.value("name").toString() : params.value("uri").toString();

    Controller* controller = frontController_ ? frontController_() : nullptr;
    if (!controller) {
        const QJsonObject errReply{{"ok", false}, {"error", QStringLiteral("No active pipeline document in GUI")}};
        socket->write(QJsonDocument(errReply).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
        services_.diagnosticLog().warn(LogChannel::Mcp, "McpBridge", QString("%1 %2 failed: no active document").arg(method, target));
        emit requestAudited(method, target, false, "No active document");
        return;
    }

    bool ok = true;
    QString errSummary;
    const QJsonObject result = core_->executeLocalRequest(controller, method, params, &ok, &errSummary);

    const QJsonObject reply{
        {"ok", ok},
        {"result", result},
        {"error", errSummary},
    };
    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();

    const QString summary = ok ? QStringLiteral("ok") : errSummary;
    if (ok)
        services_.diagnosticLog().info(LogChannel::Mcp, "McpBridge", QString("%1 %2").arg(method, target));
    else
        services_.diagnosticLog().warn(LogChannel::Mcp, "McpBridge", QString("%1 %2: %3").arg(method, target, errSummary));
    emit requestAudited(method, target, ok, summary);
}
