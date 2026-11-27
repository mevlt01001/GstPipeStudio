// mcpbridge.h – QLocalServer bridge hosted by MainWindow so external gps-mcp processes drive the live front document.
#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <functional>

#include "mcpaudit.h"

class AppServices;
class Controller;
class McpServerCore;
class QLocalSocket;

class McpBridgeServer : public QObject {
    Q_OBJECT
public:
    using ControllerProvider = std::function<Controller*()>;

    McpBridgeServer(AppServices& services, McpAuditLogger& audit, ControllerProvider frontController,
                    QObject* parent = nullptr);
    ~McpBridgeServer() override;

    bool start();
    void stop();
    bool isListening() const { return server_.isListening(); }
    QString socketPath() const { return audit_.socketPath(); }

signals:
    void requestAudited(const QString& method, const QString& target, bool ok, const QString& summary);

private:
    void onNewConnection();
    void handleSocketLine(QLocalSocket* socket, const QByteArray& line);

    AppServices& services_;
    McpAuditLogger& audit_;
    ControllerProvider frontController_;
    QLocalServer server_;
    McpServerCore* core_ = nullptr;
};
