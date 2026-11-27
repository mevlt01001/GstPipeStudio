// mcpservercore.h – JSON-RPC 2.0 + MCP (2024-11-05 / 2025-03-26) router with mandatory audit logging,
// supporting both Live GUI Proxy mode (via live.sock) and Headless in-process Controller mode.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <memory>
#include <optional>

class AppServices;
class Controller;
class McpAuditLogger;

class McpServerCore : public QObject {
    Q_OBJECT
public:
    McpServerCore(AppServices& services, McpAuditLogger& audit, bool headless, QObject* parent = nullptr);
    ~McpServerCore() override;

    // Handles one incoming JSON-RPC 2.0 message. Returns std::nullopt for notifications.
    std::optional<QJsonObject> handleMessage(const QJsonObject& request);

    // Direct execution against a caller-supplied Controller (used by headless mode and McpBridgeServer).
    QJsonObject executeLocalRequest(Controller* controller, const QString& method, const QJsonObject& params,
                                    bool* ok = nullptr, QString* errorSummary = nullptr);

private:
    Controller* ensureHeadlessController();
    bool tryForwardToLiveGui(const QString& method, const QJsonObject& params, QJsonObject* resultOut,
                             QString* errorOut);

    QJsonObject handleInitialize(const QJsonObject& params);
    QJsonObject handleResourcesList();
    QJsonObject handleResourcesRead(Controller* controller, const QJsonObject& params, bool* ok, QString* err);
    QJsonObject handlePromptsList();
    QJsonObject handlePromptsGet(const QJsonObject& params, bool* ok, QString* err);
    QJsonObject handleToolsList();
    QJsonObject handleToolsCall(Controller* controller, const QJsonObject& params, bool* ok, QString* err);

    QJsonObject toolGetStudioManual();
    QJsonObject toolSearchElements(const QJsonObject& args);
    QJsonObject toolInspectElement(const QJsonObject& args, bool* isError);
    QJsonObject toolGetPipelineState(Controller* controller);
    QJsonObject toolEditPipeline(Controller* controller, const QJsonObject& args, bool* isError);
    QJsonObject toolValidatePipeline(Controller* controller, bool* isError);
    QJsonObject toolProbePipelineMetrics(Controller* controller, const QJsonObject& args, bool* isError);
    QJsonObject toolListCustomPlugins();
    QJsonObject toolScaffoldCustomPlugin(const QJsonObject& args, bool* isError);
    QJsonObject toolUpdateCustomPluginCode(const QJsonObject& args, bool* isError);
    QJsonObject toolBuildCustomPlugin(const QJsonObject& args, bool* isError);
    QJsonObject toolExportPipelineSo(Controller* controller, const QJsonObject& args, bool* isError);
    QJsonObject toolGenerateHostIntegrationPrompt(Controller* controller, const QJsonObject& args);

    AppServices& services_;
    McpAuditLogger& audit_;
    bool headless_ = false;
    std::unique_ptr<Controller> ownedController_;
    QString clientName_;
    QString clientVersion_;
};
