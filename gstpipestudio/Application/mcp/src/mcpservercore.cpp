// mcpservercore.cpp – see mcpservercore.h.
#include "mcpservercore.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>

#include "appservices.h"
#include "buildinfo.h"
#include "contract.h"
#include "controller.h"
#include "helperclient.h"
#include "mcpaudit.h"
#include "pipelinecompiler.h"
#include "pluginlibrary.h"
#include "pluginscaffolder.h"
#include "pluginspec.h"
#include "wire.h"

namespace {

constexpr const char* kDriverManualText = R"(# GstPipeStudio MCP Driver Manual

## 1. Architecture (7-Unit Modular DAG)
1. **Pipeline** (`gstpipestudio/Pipeline/`): Pure C++17 authored `Graph`, topology rules, `GraphEditor` undo/redo, `.conf` codec, `TemplateInstance` semantics, `ElementDiagnostic`.
2. **ElementSDK** (`element_sdk/`): Pure C++17 `GstElement` custom plugin SDK (`VideoFrame`, `AudioFrame`, `ByteStreamAdapter`, `CustomMeta<T>`, `NVMM`, `Mailbox`, `BufferPool`, `DmaBuf`).
3. **RuntimeSDK** (`runtime_sdk/`): Standalone ABI-stable C99/C++17 host loader (`gps::runtime::Pipeline`: `open`, `start`, `pause`, `stop`, `setParam`, `pushFrame`, `setFrameCallback`, `metrics`).
4. **GstEngine** (`gstpipestudio/GstEngine/`): Native GStreamer inspection, `Builder`, `Importer`, lock-free `TelemetrySampler`, `Session`, and `gpsconf` plugin bridge.
5. **Application** (`gstpipestudio/Application/`): `Controller`, `EditSession`, `ValidationSession`, `RunSession`, `FactoryCatalog`, `PluginLibrary`, `PipelineCompiler`, `McpAuditLogger`, `McpServerCore`, `McpBridgeServer`.
6. **MCP (`gps-mcp`)**: Audited JSON-RPC 2.0 server + `QLocalServer` live GUI bridge.
7. **GUI** (`gstpipestudio/GUI/`): Qt6 Widgets canvas, Explorer, Plugin Studio, Telemetry Panel, and MCP Inspector.

## 2. Explicit Scope & Non-Goals
- **Does:** Design, validate, and debug arbitrary M->N GStreamer/DeepStream pipelines; scaffold/compile C++17 `element_sdk` plugins; compile authored pipelines into dual-personality `.so` modules + host integration prompts.
- **Does NOT:** Build domain-specific end-user desktop/web UIs; train ML/GAN models (wraps inference engines and exposes runtime controls); execute silent unlogged AI mutations (all MCP traffic is audited).
- **Remote GPU Offload:** Model explicitly with network/streaming elements (`udpsink`/`udpsrc`, `rtph264pay`/`depay`, `rtspsink`, `srtserversink`, `tcpclientsink`, or custom `element_sdk` nodes) and expose host-controllable parameters.

## 3. Recommended Loop: Clarify -> Plan -> Mutate -> Validate -> Export
1. **Clarify & Inspect:** Use `get_pipeline_state`, `search_elements`, `inspect_element`, and `list_custom_plugins` before inventing element names or properties.
2. **Plan:** Identify required elements, caps, exposed parameters, and any custom `element_sdk` nodes (`scaffold_custom_plugin`, `update_custom_plugin_code`, `build_custom_plugin`).
3. **Mutate Atomically:** Use `edit_pipeline` with a batch `operations` list (executes in a single Undo transaction).
4. **Validate & Probe:** Call `validate_pipeline` to inspect negotiated caps, pad reports, and `ElementDiagnostic`s; optionally call `probe_pipeline_metrics` for live `TelemetrySample` latency/drop data.
5. **Export & Handoff:** Call `export_pipeline_so` and `generate_host_integration_prompt` (e.g., for live-streamer GAN / UDP control host UIs).
)";

constexpr const char* kElementSdkGuideText = R"(# GstPipeStudio ElementSDK Guide
Custom elements are C++17 `GstElement` plugins built against `element_sdk/` using `PluginSpec` JSON:
- `id`: lowercase factory name (e.g., `myfilter`)
- `archetype`: `Auto | VideoFilter | AudioFilter | Transform | Source | Sink | GeneralElement`
- `ports`: `[{name, direction: Sink|Src, presence: Always|Request|Sometimes, capsPreset: VideoRaw|VideoNvmm|AudioRaw|ByteStream|CustomMime|Any, capsDetail, inputMode: Direct|Mailbox|BoundedQueue, mailboxDefault, queueCapacity, overflowPolicy}]`
- `properties`: `[{name, nick, blurb, type: Bool|Int|UInt|Int64|UInt64|Float|Double|String, defaultValue, minValue, maxValue, mutability: Null|Ready|Paused|Playing, controllable}]`
- `coordination`: `Independent | AllReady | PtsWindow`, `toleranceMs`, `live`, `clockSync`
Use `scaffold_custom_plugin` -> `update_custom_plugin_code` -> `build_custom_plugin`.
)";

constexpr const char* kRuntimeSdkGuideText = R"(# GstPipeStudio RuntimeSDK Guide
`export_pipeline_so` compiles the active pipeline into a dual-personality `.so`:
1. **C/C++ Host Module (`gps_pipeline_get_module_v1`):** Loaded via `gps::runtime::Pipeline` (`runtime_sdk/`):
   - `open(soPath, config)`, `start()`, `pause()`, `stop()`
   - `setParam(name, value)` / `getParam(name)` for every `[expose:<name>]` parameter
   - `pushFrame(...)` / `setFrameCallback(...)` for `appsrc`/`appsink` boundaries
   - `metrics()` for lock-free per-element latency & drop telemetry
2. **Native GstBin Plugin:** Usable directly inside `gst-launch-1.0` or GStreamer pipelines.
)";

QJsonObject toolTextResult(const QString& text, bool isError = false) {
    return {
        {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}},
        {"isError", isError},
    };
}

QJsonObject toolJsonResult(const QJsonObject& obj, bool isError = false) {
    return toolTextResult(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)), isError);
}

QJsonObject jsonRpcResult(const QJsonValue& id, const QJsonObject& result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject jsonRpcError(const QJsonValue& id, int code, const QString& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject serializeUpdateResult(const gps::UpdateResult& r) {
    QJsonArray pads;
    for (const auto& p : r.pads) {
        pads.append(QJsonObject{
            {"element", fromStd(p.element)},
            {"pad", fromStd(p.pad)},
            {"identity", fromStd(p.identity)},
            {"templateName", fromStd(p.templateName)},
            {"presence", fromStd(p.presence)},
            {"isSrc", p.isSrc},
            {"linked", p.linked},
            {"temporaryEnd", p.temporaryEnd},
            {"unknownData", p.unknownData},
            {"caps", fromStd(p.caps)},
            {"allowed", fromStd(p.allowed)},
        });
    }
    QJsonArray diagnostics;
    for (const auto& d : r.diagnostics) {
        diagnostics.append(QJsonObject{
            {"element", fromStd(d.element)},
            {"pad", fromStd(d.pad)},
            {"message", fromStd(d.message)},
            {"isError", d.isError},
        });
    }
    QJsonArray missing;
    for (const auto& m : r.missing) missing.append(fromStd(m));
    QJsonArray refused;
    for (const auto& ref : r.refused) refused.append(fromStd(ref));

    return {
        {"status", fromStd(r.status)},
        {"message", fromStd(r.message)},
        {"errorElement", fromStd(r.errorElement)},
        {"ms", r.ms},
        {"complete", r.complete()},
        {"pads", pads},
        {"diagnostics", diagnostics},
        {"missing", missing},
        {"refused", refused},
    };
}

QJsonObject serializeGraph(const gps::Graph& g) {
    QJsonArray elements;
    for (const auto& e : g.elements) {
        QJsonObject props;
        for (const auto& [k, v] : e.props) props.insert(fromStd(k), fromStd(v));
        QJsonArray pads;
        for (const auto& p : e.pads) {
            pads.append(QJsonObject{
                {"identity", fromStd(p.identity)},
                {"direction", p.direction == gps::Pad::Direction::Source ? "src" : "sink"},
                {"templateCaps", fromStd(p.templateCaps)},
                {"negotiatedCaps", fromStd(p.negotiatedCaps)},
                {"allowedCaps", fromStd(p.allowedCaps)},
            });
        }
        elements.append(QJsonObject{
            {"name", fromStd(e.name)},
            {"factory", fromStd(e.factory)},
            {"group", fromStd(e.group)},
            {"props", props},
            {"x", e.x},
            {"y", e.y},
            {"pads", pads},
        });
    }
    QJsonArray links;
    for (const auto& l : g.links) {
        links.append(QJsonObject{
            {"srcElement", fromStd(l.srcElement)},
            {"srcPad", fromStd(l.srcPad)},
            {"sinkElement", fromStd(l.sinkElement)},
            {"sinkPad", fromStd(l.sinkPad)},
            {"caps", fromStd(l.caps)},
        });
    }
    QJsonArray exposed;
    for (const auto& ex : g.exposed) {
        exposed.append(QJsonObject{
            {"name", fromStd(ex.name)},
            {"element", fromStd(ex.element)},
            {"property", fromStd(ex.property)},
        });
    }
    return {
        {"name", fromStd(g.name)},
        {"elements", elements},
        {"links", links},
        {"exposed", exposed},
    };
}

QJsonObject makeTool(const QString& name, const QString& description, const QJsonObject& properties,
                     const QStringList& required = {}) {
    QJsonArray req;
    for (const QString& r : required) req.append(r);
    return {
        {"name", name},
        {"description", description},
        {"inputSchema", QJsonObject{{"type", "object"}, {"properties", properties}, {"required", req}}},
    };
}

}  // namespace

McpServerCore::McpServerCore(AppServices& services, McpAuditLogger& audit, bool headless, QObject* parent)
    : QObject(parent), services_(services), audit_(audit), headless_(headless) {}

McpServerCore::~McpServerCore() = default;

Controller* McpServerCore::ensureHeadlessController() {
    if (!ownedController_) ownedController_ = std::make_unique<Controller>(services_);
    return ownedController_.get();
}

bool McpServerCore::tryForwardToLiveGui(const QString& method, const QJsonObject& params, QJsonObject* resultOut,
                                        QString* errorOut) {
    if (headless_) return false;
    const QString sock = audit_.socketPath();
    if (!QFileInfo::exists(sock)) return false;

    QLocalSocket socket;
    socket.connectToServer(sock);
    if (!socket.waitForConnected(400)) return false;

    const QJsonObject envelope{
        {"method", method},
        {"params", params},
        {"clientName", clientName_},
        {"clientVersion", clientVersion_},
    };
    socket.write(QJsonDocument(envelope).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();

    QByteArray line;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 35000) {
        if (!socket.waitForReadyRead(1000)) {
            if (socket.state() == QLocalSocket::UnconnectedState) break;
            continue;
        }
        line.append(socket.readAll());
        const int nl = line.indexOf('\n');
        if (nl >= 0) {
            line = line.left(nl).trimmed();
            break;
        }
    }
    if (line.isEmpty()) {
        if (errorOut) *errorOut = "Timed out waiting for live GUI bridge response";
        return true;
    }

    const QJsonObject reply = QJsonDocument::fromJson(line).object();
    if (!reply.value("ok").toBool(false)) {
        if (errorOut) *errorOut = reply.value("error").toString("Live GUI bridge error");
    }
    if (resultOut) *resultOut = reply.value("result").toObject();
    return true;
}

std::optional<QJsonObject> McpServerCore::handleMessage(const QJsonObject& request) {
    const QString method = request.value("method").toString();
    const QJsonValue id = request.value("id");
    const bool hasId = request.contains("id") && !id.isNull();
    const QJsonObject params = request.value("params").toObject();

    if (method == "notifications/initialized" || !hasId) {
        return std::nullopt;
    }

    QElapsedTimer timer;
    timer.start();

    QJsonObject response;
    QString mode = "local";
    QString target;
    bool ok = true;
    QString errSummary;

    if (method == "initialize") {
        response = jsonRpcResult(id, handleInitialize(params));
    } else if (method == "ping") {
        response = jsonRpcResult(id, QJsonObject{});
    } else if (method == "resources/list") {
        response = jsonRpcResult(id, handleResourcesList());
    } else if (method == "prompts/list") {
        response = jsonRpcResult(id, handlePromptsList());
    } else if (method == "tools/list") {
        response = jsonRpcResult(id, handleToolsList());
    } else if (method == "resources/read" || method == "tools/call" || method == "prompts/get") {
        target = method == "tools/call"   ? params.value("name").toString()
               : method == "prompts/get"  ? params.value("name").toString()
                                          : params.value("uri").toString();
        QJsonObject result;
        bool forwarded = false;
        if (method == "tools/call" || (method == "resources/read" && target.startsWith("gps://workspace/"))) {
            forwarded = tryForwardToLiveGui(method, params, &result, &errSummary);
        }
        if (forwarded) {
            mode = "live-gui";
            ok = errSummary.isEmpty() && !result.value("isError").toBool(false);
            if (result.isEmpty() && !errSummary.isEmpty())
                response = jsonRpcError(id, -32000, errSummary);
            else
                response = jsonRpcResult(id, result);
        } else {
            mode = headless_ ? "headless" : "local-fallback";
            result = executeLocalRequest(ensureHeadlessController(), method, params, &ok, &errSummary);
            if (!ok && result.isEmpty())
                response = jsonRpcError(id, -32602, errSummary);
            else
                response = jsonRpcResult(id, result);
        }
    } else {
        ok = false;
        errSummary = QString("Method not found: %1").arg(method);
        response = jsonRpcError(id, -32601, errSummary);
    }

    const bool mustAudit = (method == "initialize" || method == "tools/call" || method == "resources/read" ||
                            method == "prompts/get");
    if (mustAudit) {
        McpAuditEntry entry;
        entry.clientName = clientName_;
        entry.clientVersion = clientVersion_;
        entry.mode = mode;
        entry.method = method;
        entry.toolOrResource = target;
        entry.requestJson = QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
        entry.responseJson = QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
        entry.durationMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        entry.ok = ok;
        entry.errorSummary = errSummary;
        audit_.record(std::move(entry));
    }

    return response;
}

QJsonObject McpServerCore::executeLocalRequest(Controller* controller, const QString& method, const QJsonObject& params,
                                                bool* ok, QString* errorSummary) {
    bool localOk = true;
    QString localErr;
    QJsonObject res;

    if (method == "resources/read")
        res = handleResourcesRead(controller, params, &localOk, &localErr);
    else if (method == "prompts/get")
        res = handlePromptsGet(params, &localOk, &localErr);
    else if (method == "tools/call")
        res = handleToolsCall(controller, params, &localOk, &localErr);
    else {
        localOk = false;
        localErr = QString("Unsupported local request: %1").arg(method);
    }

    if (ok) *ok = localOk;
    if (errorSummary) *errorSummary = localErr;
    return res;
}

QJsonObject McpServerCore::handleInitialize(const QJsonObject& params) {
    const QJsonObject clientInfo = params.value("clientInfo").toObject();
    clientName_ = clientInfo.value("name").toString("unknown");
    clientVersion_ = clientInfo.value("version").toString();

    QString version = params.value("protocolVersion").toString();
    if (version != "2024-11-05" && version != "2025-03-26") version = "2025-03-26";

    return {
        {"protocolVersion", version},
        {"capabilities", QJsonObject{{"tools", QJsonObject{}}, {"resources", QJsonObject{}}, {"prompts", QJsonObject{}}}},
        {"serverInfo", QJsonObject{{"name", "gps-mcp"}, {"version", QStringLiteral(GPS_APP_VERSION)}}},
        {"instructions", QString::fromUtf8(kDriverManualText)},
    };
}

QJsonObject McpServerCore::handleResourcesList() {
    return {
        {"resources", QJsonArray{
            QJsonObject{{"uri", "gps://guide/driver-manual"}, {"name", "Driver Manual"}, {"mimeType", "text/markdown"}},
            QJsonObject{{"uri", "gps://guide/element-sdk"}, {"name", "ElementSDK Guide"}, {"mimeType", "text/markdown"}},
            QJsonObject{{"uri", "gps://guide/runtime-sdk"}, {"name", "RuntimeSDK Guide"}, {"mimeType", "text/markdown"}},
            QJsonObject{{"uri", "gps://workspace/active-pipeline"}, {"name", "Active Pipeline"}, {"mimeType", "application/json"}},
            QJsonObject{{"uri", "gps://workspace/custom-plugins"}, {"name", "Custom Plugins"}, {"mimeType", "application/json"}},
        }},
    };
}

QJsonObject McpServerCore::handleResourcesRead(Controller* controller, const QJsonObject& params, bool* ok, QString* err) {
    const QString uri = params.value("uri").toString();
    QString mime = "text/markdown";
    QString text;

    if (uri == "gps://guide/driver-manual") {
        text = QString::fromUtf8(kDriverManualText);
    } else if (uri == "gps://guide/element-sdk") {
        text = QString::fromUtf8(kElementSdkGuideText);
    } else if (uri == "gps://guide/runtime-sdk") {
        text = QString::fromUtf8(kRuntimeSdkGuideText);
    } else if (uri == "gps://workspace/active-pipeline") {
        mime = "application/json";
        const QJsonObject state = {
            {"graph", serializeGraph(controller->plan())},
            {"validation", serializeUpdateResult(controller->result())},
        };
        text = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Indented));
    } else if (uri == "gps://workspace/custom-plugins") {
        mime = "application/json";
        text = toolListCustomPlugins().value("content").toArray().at(0).toObject().value("text").toString();
    } else {
        if (ok) *ok = false;
        if (err) *err = QString("Unknown resource URI: %1").arg(uri);
        return {};
    }

    if (ok) *ok = true;
    return {{"contents", QJsonArray{QJsonObject{{"uri", uri}, {"mimeType", mime}, {"text", text}}}}};
}

QJsonObject McpServerCore::handlePromptsList() {
    return {
        {"prompts", QJsonArray{
            QJsonObject{{"name", "architect_pipeline"},
                        {"description", "Clarify-first GStreamer/DeepStream pipeline architecture workflow"},
                        {"arguments", QJsonArray{QJsonObject{{"name", "goal"}, {"required", false}}}}},
            QJsonObject{{"name", "create_custom_element"},
                        {"description", "Scaffold, implement, and build a C++17 element_sdk GstElement"},
                        {"arguments", QJsonArray{QJsonObject{{"name", "name"}, {"required", false}},
                                                 QJsonObject{{"name", "purpose"}, {"required", false}}}}},
            QJsonObject{{"name", "host_ui_integration_handoff"},
                        {"description", "Export compiled .so and generate Host UI Team Integration Prompt"},
                        {"arguments", QJsonArray{QJsonObject{{"name", "moduleName"}, {"required", false}},
                                                 QJsonObject{{"name", "targetAudience"}, {"required", false}}}}},
        }},
    };
}

QJsonObject McpServerCore::handlePromptsGet(const QJsonObject& params, bool* ok, QString* err) {
    const QString name = params.value("name").toString();
    const QJsonObject args = params.value("arguments").toObject();
    QString desc;
    QString text;

    if (name == "architect_pipeline") {
        desc = "Clarify-first pipeline architecture workflow";
        text = QString(
                   "Follow Clarify -> Plan -> Mutate -> Validate:\n"
                   "1. Goal: %1\n"
                   "2. Call get_pipeline_state and search_elements/inspect_element to verify available GStreamer elements.\n"
                   "3. Clarify ambiguous media formats, hardware acceleration, or network/UDP offload requirements.\n"
                   "4. Mutate atomically via edit_pipeline, then call validate_pipeline and probe_pipeline_metrics.")
                   .arg(args.value("goal").toString("Design a real-time streaming pipeline"));
    } else if (name == "create_custom_element") {
        desc = "Create custom element_sdk plugin";
        text = QString(
                   "Create custom GstElement '%1' (%2):\n"
                   "1. Read gps://guide/element-sdk.\n"
                   "2. Call scaffold_custom_plugin with a valid PluginSpec JSON.\n"
                   "3. Implement processing logic via update_custom_plugin_code and compile with build_custom_plugin.")
                   .arg(args.value("name").toString("customnode"),
                        args.value("purpose").toString("custom frame/buffer processing"));
    } else if (name == "host_ui_integration_handoff") {
        desc = "Host UI integration handoff";
        text = QString(
                   "Prepare host integration handoff for module '%1' (audience: %2):\n"
                   "1. Ensure all runtime-adjustable properties are exposed via edit_pipeline (expose_param).\n"
                   "2. Call validate_pipeline, then export_pipeline_so.\n"
                   "3. Call generate_host_integration_prompt and deliver the resulting INTEGRATION.md prompt.")
                   .arg(args.value("moduleName").toString("pipeline_engine"),
                        args.value("targetAudience").toString("Host UI Team"));
    } else {
        if (ok) *ok = false;
        if (err) *err = QString("Unknown prompt: %1").arg(name);
        return {};
    }

    if (ok) *ok = true;
    return {
        {"description", desc},
        {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", QJsonObject{{"type", "text"}, {"text", text}}}}}},
    };
}

QJsonObject McpServerCore::handleToolsList() {
    const QJsonArray tools{
        makeTool("get_studio_manual", "Return the GstPipeStudio MCP Driver Manual and architecture guide.", {}),
        makeTool("search_elements", "Search available GStreamer/DeepStream element factories.",
                 {{"query", QJsonObject{{"type", "string"}}},
                  {"role", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"source", "filter", "sink"}}}},
                  {"caps", QJsonObject{{"type", "string"}}},
                  {"limit", QJsonObject{{"type", "integer"}}}}),
        makeTool("inspect_element", "Inspect a GStreamer element factory's pads, properties, and caps.",
                 {{"factory", QJsonObject{{"type", "string"}}}}, {"factory"}),
        makeTool("get_pipeline_state", "Get the active pipeline graph, exposed params, and last validation diagnostics.", {}),
        makeTool("edit_pipeline", "Apply an atomic batch of graph mutations as a single undoable transaction.",
                 {{"operations", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "object"}}}}}},
                 {"operations"}),
        makeTool("validate_pipeline", "Validate the active pipeline with gps-update and return diagnostics & negotiated pads.", {}),
        makeTool("probe_pipeline_metrics", "Run a short gps-monitor probe on the active pipeline and return telemetry samples.",
                 {{"durationMs", QJsonObject{{"type", "integer"}}}}),
        makeTool("list_custom_plugins", "List custom element_sdk plugins in the workspace library.", {}),
        makeTool("scaffold_custom_plugin", "Create a new custom element_sdk plugin from a PluginSpec JSON.",
                 {{"spec", QJsonObject{{"type", "object"}}}}, {"spec"}),
        makeTool("update_custom_plugin_code", "Update the C++17 source code of a custom plugin.",
                 {{"id", QJsonObject{{"type", "string"}}}, {"source", QJsonObject{{"type", "string"}}}},
                 {"id", "source"}),
        makeTool("build_custom_plugin", "Compile and install a custom element_sdk plugin.",
                 {{"id", QJsonObject{{"type", "string"}}}}, {"id"}),
        makeTool("export_pipeline_so", "Compile the active pipeline into a dual-personality .so module.",
                 {{"moduleName", QJsonObject{{"type", "string"}}},
                  {"outputDir", QJsonObject{{"type", "string"}}},
                  {"version", QJsonObject{{"type", "string"}}},
                  {"description", QJsonObject{{"type", "string"}}}},
                 {"moduleName", "outputDir"}),
        makeTool("generate_host_integration_prompt",
                 "Generate a Host UI Team Integration Prompt for embedding the exported pipeline .so.",
                 {{"moduleName", QJsonObject{{"type", "string"}}},
                  {"targetAudience", QJsonObject{{"type", "string"}}},
                  {"extraNotes", QJsonObject{{"type", "string"}}}},
                 {"moduleName"}),
    };
    return {{"tools", tools}};
}

QJsonObject McpServerCore::handleToolsCall(Controller* controller, const QJsonObject& params, bool* ok, QString* err) {
    const QString name = params.value("name").toString();
    const QJsonObject args = params.value("arguments").toObject();
    bool isError = false;
    QJsonObject res;

    if (name == "get_studio_manual")
        res = toolGetStudioManual();
    else if (name == "search_elements")
        res = toolSearchElements(args);
    else if (name == "inspect_element")
        res = toolInspectElement(args, &isError);
    else if (name == "get_pipeline_state")
        res = toolGetPipelineState(controller);
    else if (name == "edit_pipeline")
        res = toolEditPipeline(controller, args, &isError);
    else if (name == "validate_pipeline")
        res = toolValidatePipeline(controller, &isError);
    else if (name == "probe_pipeline_metrics")
        res = toolProbePipelineMetrics(controller, args, &isError);
    else if (name == "list_custom_plugins")
        res = toolListCustomPlugins();
    else if (name == "scaffold_custom_plugin")
        res = toolScaffoldCustomPlugin(args, &isError);
    else if (name == "update_custom_plugin_code")
        res = toolUpdateCustomPluginCode(args, &isError);
    else if (name == "build_custom_plugin")
        res = toolBuildCustomPlugin(args, &isError);
    else if (name == "export_pipeline_so")
        res = toolExportPipelineSo(controller, args, &isError);
    else if (name == "generate_host_integration_prompt")
        res = toolGenerateHostIntegrationPrompt(controller, args);
    else {
        if (ok) *ok = false;
        if (err) *err = QString("Unknown tool: %1").arg(name);
        return toolTextResult(*err, true);
    }

    if (ok) *ok = !isError;
    if (err && isError) {
        const QJsonArray content = res.value("content").toArray();
        *err = content.isEmpty() ? QStringLiteral("Tool failed") : content.at(0).toObject().value("text").toString();
    }
    return res;
}

QJsonObject McpServerCore::toolGetStudioManual() { return toolTextResult(QString::fromUtf8(kDriverManualText)); }

QJsonObject McpServerCore::toolSearchElements(const QJsonObject& args) {
    FactoryCatalog& catalog = services_.catalog();
    if (!catalog.loaded()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&catalog, &FactoryCatalog::ready, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        catalog.load();
        timer.start(10000);
        if (!catalog.loaded()) loop.exec();
    }

    const QString query = args.value("query").toString().trimmed();
    const QString role = args.value("role").toString().trimmed().toLower();
    const QString caps = args.value("caps").toString().trimmed();
    const int limit = std::clamp(args.value("limit").toInt(50), 1, 200);

    QJsonArray matches;
    int total = 0;
    for (const auto& e : catalog.entries()) {
        if (role == "source" && !e.isSource) continue;
        if (role == "sink" && !e.isSink) continue;
        if (role == "filter" && !e.isFilter()) continue;

        const QString factory = fromStd(e.factory);
        const QString longName = fromStd(e.longName);
        const QString klass = fromStd(e.klass);
        const QString plugin = fromStd(e.plugin);

        if (!query.isEmpty() && !factory.contains(query, Qt::CaseInsensitive) &&
            !longName.contains(query, Qt::CaseInsensitive) && !klass.contains(query, Qt::CaseInsensitive) &&
            !plugin.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        if (!caps.isEmpty()) {
            if (const ElementInfo* info = catalog.info(factory)) {
                bool capsMatch = false;
                for (const auto& pt : info->padTemplates) {
                    if (fromStd(pt.caps).contains(caps, Qt::CaseInsensitive)) {
                        capsMatch = true;
                        break;
                    }
                }
                if (!capsMatch) continue;
            }
        }
        ++total;
        if (matches.size() < limit) {
            matches.append(QJsonObject{
                {"factory", factory},
                {"longName", longName},
                {"klass", klass},
                {"plugin", plugin},
                {"isSource", e.isSource},
                {"isSink", e.isSink},
                {"nvidia", e.nvidia},
                {"rank", e.rank},
            });
        }
    }
    return toolJsonResult({{"elements", matches}, {"total", total}});
}

QJsonObject McpServerCore::toolInspectElement(const QJsonObject& args, bool* isError) {
    const QString factory = args.value("factory").toString().trimmed();
    if (factory.isEmpty()) {
        *isError = true;
        return toolTextResult("Missing required argument: factory", true);
    }

    const ElementInfo* cached = services_.catalog().info(factory);
    if (!cached) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        services_.catalog().fetchInfo(factory, this, [&](const ElementInfo* info) {
            cached = info;
            loop.quit();
        });
        timer.start(10000);
        if (!cached) loop.exec();
    }

    if (!cached) {
        *isError = true;
        return toolTextResult(QString("Factory not found or inspection timed out: %1").arg(factory), true);
    }

    QJsonArray padTemplates;
    for (const auto& pt : cached->padTemplates) {
        padTemplates.append(QJsonObject{
            {"name", fromStd(pt.name)},
            {"direction", fromStd(pt.direction)},
            {"presence", fromStd(pt.presence)},
            {"caps", fromStd(pt.caps)},
        });
    }
    QJsonArray properties;
    for (const auto& prop : cached->properties) {
        QJsonArray enums;
        for (const auto& ev : prop.enumValues) {
            enums.append(QJsonObject{{"nick", fromStd(ev.nick)}, {"name", fromStd(ev.name)}, {"value", fromStd(ev.value)}});
        }
        properties.append(QJsonObject{
            {"name", fromStd(prop.name)},
            {"nick", fromStd(prop.nick)},
            {"blurb", fromStd(prop.blurb)},
            {"kind", fromStd(prop.kind)},
            {"typeName", fromStd(prop.typeName)},
            {"defaultValue", fromStd(prop.defaultValue)},
            {"min", fromStd(prop.min)},
            {"max", fromStd(prop.max)},
            {"writable", prop.writable},
            {"readable", prop.readable},
            {"enumValues", enums},
        });
    }

    return toolJsonResult({
        {"factory", fromStd(cached->entry.factory)},
        {"longName", fromStd(cached->entry.longName)},
        {"klass", fromStd(cached->entry.klass)},
        {"plugin", fromStd(cached->entry.plugin)},
        {"description", fromStd(cached->description)},
        {"author", fromStd(cached->author)},
        {"padTemplates", padTemplates},
        {"properties", properties},
    });
}

QJsonObject McpServerCore::toolGetPipelineState(Controller* controller) {
    return toolJsonResult({
        {"graph", serializeGraph(controller->plan())},
        {"validation", serializeUpdateResult(controller->result())},
        {"busy", controller->busy()},
        {"busyReason", controller->busyReason()},
    });
}

QJsonObject McpServerCore::toolEditPipeline(Controller* controller, const QJsonObject& args, bool* isError) {
    const QJsonArray ops = args.value("operations").toArray();
    if (ops.isEmpty()) {
        *isError = true;
        return toolTextResult("edit_pipeline requires a non-empty 'operations' array", true);
    }

    QStringList factoriesToFetch;
    for (const auto& val : ops) {
        const QJsonObject op = val.toObject();
        if (op.value("op").toString() == "add_element") {
            const QString f = op.value("factory").toString();
            if (!f.isEmpty() && !factoriesToFetch.contains(f)) factoriesToFetch.append(f);
        }
    }
    if (!factoriesToFetch.isEmpty()) {
        bool missingAny = false;
        for (const QString& f : factoriesToFetch) {
            if (!services_.catalog().info(f)) {
                missingAny = true;
                break;
            }
        }
        if (missingAny) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            services_.catalog().fetchInfos(factoriesToFetch, this, [&](bool) { loop.quit(); });
            timer.start(10000);
            loop.exec();
        }
    }

    QStringList created;
    bool wantAlign = false;
    QStringList launchDescriptions;
    QString err;

    const bool ok = controller->applyBatchEdit("MCP edit_pipeline", [&](gps::Graph& g, QString* localErr) {
        QHash<QString, QString> aliases;
        auto resolve = [&](const QString& name) { return aliases.value(name, name); };

        for (int i = 0; i < ops.size(); ++i) {
            const QJsonObject opObj = ops.at(i).toObject();
            const QString op = opObj.value("op").toString();

            if (op == "clear") {
                g = gps::Graph{};
                aliases.clear();
            } else if (op == "add_element") {
                const QString factory = opObj.value("factory").toString();
                if (factory.isEmpty()) {
                    *localErr = QString("op[%1] add_element missing 'factory'").arg(i);
                    return false;
                }
                gps::Props props;
                const QJsonObject pObj = opObj.value("props").toObject();
                for (auto it = pObj.begin(); it != pObj.end(); ++it) {
                    props[toStd(it.key())] = toStd(it.value().toVariant().toString());
                }
                const double x = opObj.value("x").toDouble(0.0);
                const double y = opObj.value("y").toDouble(0.0);
                const std::string added = g.addElement(toStd(factory), props, x, y);
                const QString addedQ = fromStd(added);
                created.append(addedQ);
                const QString alias = opObj.value("id").toString(opObj.value("alias").toString());
                if (!alias.isEmpty()) aliases.insert(alias, addedQ);
            } else if (op == "remove_element") {
                const QString elem = resolve(opObj.value("element").toString());
                const bool autoHeal = opObj.value("autoHeal").toBool(true);
                if (!g.deleteElement(toStd(elem), autoHeal)) {
                    *localErr = QString("op[%1] remove_element failed for '%2'").arg(i).arg(elem);
                    return false;
                }
            } else if (op == "set_property") {
                const QString elem = resolve(opObj.value("element").toString());
                gps::Element* e = g.find(toStd(elem));
                if (!e) {
                    *localErr = QString("op[%1] set_property: element '%2' not found").arg(i).arg(elem);
                    return false;
                }
                gps::Props props = e->props;
                if (opObj.contains("properties") && opObj.value("properties").isObject()) {
                    const QJsonObject pObj = opObj.value("properties").toObject();
                    for (auto it = pObj.begin(); it != pObj.end(); ++it) {
                        if (it.value().isNull())
                            props.erase(toStd(it.key()));
                        else
                            props[toStd(it.key())] = toStd(it.value().toVariant().toString());
                    }
                } else {
                    const QString prop = opObj.value("property").toString();
                    if (prop.isEmpty()) {
                        *localErr = QString("op[%1] set_property missing 'property'").arg(i);
                        return false;
                    }
                    if (opObj.value("value").isNull())
                        props.erase(toStd(prop));
                    else
                        props[toStd(prop)] = toStd(opObj.value("value").toVariant().toString());
                }
                if (!g.setProps(e->name, props)) {
                    *localErr = QString("op[%1] set_property failed for '%2'").arg(i).arg(elem);
                    return false;
                }
            } else if (op == "expose_param") {
                const QString paramName = opObj.value("name").toString();
                const QString elem = resolve(opObj.value("element").toString());
                const QString prop = opObj.value("property").toString();
                if (paramName.isEmpty() || elem.isEmpty() || prop.isEmpty()) {
                    *localErr = QString("op[%1] expose_param requires name, element, property").arg(i);
                    return false;
                }
                auto exposed = g.exposed;
                auto it = std::find_if(exposed.begin(), exposed.end(),
                                       [&](const gps::ExposedParam& p) { return p.name == toStd(paramName); });
                if (it != exposed.end()) {
                    it->element = toStd(elem);
                    it->property = toStd(prop);
                } else {
                    exposed.push_back({toStd(paramName), toStd(elem), toStd(prop)});
                }
                g.setExposed(exposed);
            } else if (op == "connect") {
                const QString src = resolve(opObj.value("src").toString());
                const QString srcPad = opObj.value("srcPad").toString("src");
                const QString sink = resolve(opObj.value("sink").toString());
                const QString sinkPad = opObj.value("sinkPad").toString("sink");
                const QString caps = opObj.value("caps").toString();
                if (!g.linkExisting(toStd(src), toStd(srcPad), toStd(sink), toStd(sinkPad), toStd(caps))) {
                    *localErr = QString("op[%1] connect failed: %2.%3 -> %4.%5").arg(i).arg(src, srcPad, sink, sinkPad);
                    return false;
                }
            } else if (op == "disconnect") {
                const QString src = resolve(opObj.value("src").toString());
                const QString srcPad = opObj.value("srcPad").toString("src");
                if (!g.disconnectLink(toStd(src), toStd(srcPad))) {
                    *localErr = QString("op[%1] disconnect failed: %2.%3").arg(i).arg(src, srcPad);
                    return false;
                }
            } else if (op == "set_link_caps") {
                const QString src = resolve(opObj.value("src").toString());
                const QString srcPad = opObj.value("srcPad").toString("src");
                const QString caps = opObj.value("caps").toString();
                if (!g.setCaps(toStd(src), toStd(srcPad), toStd(caps))) {
                    *localErr = QString("op[%1] set_link_caps failed: %2.%3").arg(i).arg(src, srcPad);
                    return false;
                }
            } else if (op == "add_gst_launch") {
                const QString desc = opObj.value("description").toString();
                if (desc.isEmpty()) {
                    *localErr = QString("op[%1] add_gst_launch missing 'description'").arg(i);
                    return false;
                }
                launchDescriptions.append(desc);
            } else if (op == "auto_align") {
                wantAlign = true;
            } else {
                *localErr = QString("op[%1] unknown operation '%2'").arg(i).arg(op);
                return false;
            }
        }
        return true;
    }, &err);

    if (!ok) {
        *isError = true;
        return toolTextResult(err.isEmpty() ? QStringLiteral("edit_pipeline rejected") : err, true);
    }

    for (const QString& desc : launchDescriptions) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        controller->addLaunch(desc, Controller::kFree, Controller::kFree, [&](const QStringList& added) {
            created.append(added);
            loop.quit();
        });
        timer.start(15000);
        loop.exec();
    }

    if (wantAlign) controller->niceAlign();

    QJsonArray createdArr;
    for (const QString& c : created) createdArr.append(c);
    return toolJsonResult({{"ok", true}, {"created", createdArr}, {"graph", serializeGraph(controller->plan())}});
}

QJsonObject McpServerCore::toolValidatePipeline(Controller* controller, bool* isError) {
    if (controller->plan().empty()) {
        return toolJsonResult({
            {"status", "empty"},
            {"message", "Pipeline is empty"},
            {"complete", false},
            {"diagnostics", QJsonArray{}},
            {"pads", QJsonArray{}},
        });
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool finished = false;
    auto conn = QObject::connect(controller, &Controller::updateFinished, &loop, [&](bool) {
        finished = true;
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    if (!controller->refresh()) {
        if (!controller->updating()) {
            QObject::disconnect(conn);
            *isError = true;
            return toolTextResult(controller->busyReason().isEmpty() ? "Unable to start validation" : controller->busyReason(),
                                  true);
        }
    }
    timer.start(30000);
    if (!finished) loop.exec();
    QObject::disconnect(conn);

    if (!finished) {
        *isError = true;
        return toolTextResult("validate_pipeline timed out waiting for gps-update", true);
    }
    return toolJsonResult(serializeUpdateResult(controller->result()));
}

QJsonObject McpServerCore::toolProbePipelineMetrics(Controller* controller, const QJsonObject& args, bool* isError) {
    if (controller->plan().empty()) {
        *isError = true;
        return toolTextResult("Cannot probe empty pipeline", true);
    }

    const int durationMs = std::clamp(args.value("durationMs").toInt(1500), 200, 15000);
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        *isError = true;
        return toolTextResult("Failed to create temporary directory for probe", true);
    }
    const QString confPath = tmp.filePath("probe.conf");
    std::string saveErr;
    if (!controller->plan().save(toStd(confPath), &saveErr)) {
        *isError = true;
        return toolTextResult(fromStd(saveErr), true);
    }

    QEventLoop loop;
    QJsonArray samples;
    QJsonArray latestElements;
    HelperResult finalResult;
    bool gotResult = false;

    HelperClient* client = HelperClient::run(
        this, "gps-monitor", {confPath}, durationMs + 5000, [](const QString&) {},
        [&](const HelperResult& res) {
            finalResult = res;
            gotResult = true;
            loop.quit();
        },
        [&](const QString& line) {
            gps::TelemetrySample sample;
            if (gps::fromJson(toStd(line), &sample)) {
                QJsonArray elems;
                for (const auto& e : sample.elements) {
                    elems.append(QJsonObject{
                        {"element", fromStd(e.element)},
                        {"ageMs", e.ageMs},
                        {"queueMs", e.queueMs},
                        {"processingMs", e.processingMs},
                        {"pendingMs", e.pendingMs},
                        {"processingCycles", static_cast<qint64>(e.processingCycles)},
                        {"dropRatePercent", e.dropRatePercent},
                        {"qosProcessed", static_cast<qint64>(e.qosProcessed)},
                        {"qosDropped", static_cast<qint64>(e.qosDropped)},
                        {"inputBuffers", static_cast<qint64>(e.inputBuffers)},
                        {"outputBuffers", static_cast<qint64>(e.outputBuffers)},
                    });
                }
                latestElements = elems;
                samples.append(QJsonObject{{"elapsedMs", sample.elapsedMs}, {"elements", elems}});
            }
        });

    QTimer::singleShot(durationMs, client, [client] { client->interrupt(2000); });
    if (!gotResult) loop.exec();

    gps::RunResult runRes;
    finalResult.read(&runRes);
    return toolJsonResult({
        {"status", fromStd(runRes.status)},
        {"message", fromStd(runRes.message)},
        {"ms", runRes.ms},
        {"elements", latestElements},
        {"samples", samples},
    });
}

QJsonObject McpServerCore::toolListCustomPlugins() {
    QJsonArray plugins;
    for (const auto& info : services_.plugins().components()) {
        const QString id = info.ref.id;
        plugins.append(QJsonObject{
            {"id", id},
            {"title", info.title},
            {"description", info.description},
            {"status", services_.plugins().statusText(id)},
            {"sourcePath", services_.plugins().sourcePath(id)},
            {"publishedModule", services_.plugins().publishedModule(id)},
        });
    }
    return toolJsonResult({{"plugins", plugins}});
}

QJsonObject McpServerCore::toolScaffoldCustomPlugin(const QJsonObject& args, bool* isError) {
    const QJsonObject specObj = args.contains("spec") ? args.value("spec").toObject() : args;
    PluginSpec spec;
    QString err;
    if (!PluginSpec::fromJson(specObj, &spec, &err)) {
        *isError = true;
        return toolTextResult(err.isEmpty() ? "Invalid PluginSpec JSON" : err, true);
    }
    if (!services_.plugins().createPlugin(spec, &err)) {
        *isError = true;
        return toolTextResult(err, true);
    }
    return toolJsonResult({
        {"ok", true},
        {"id", spec.id},
        {"directory", services_.plugins().directory(spec.id)},
        {"sourcePath", services_.plugins().sourcePath(spec.id)},
    });
}

QJsonObject McpServerCore::toolUpdateCustomPluginCode(const QJsonObject& args, bool* isError) {
    const QString id = args.value("id").toString();
    const QString source = args.value("source").toString();
    QString err;
    if (!services_.plugins().saveSource(id, source, &err)) {
        *isError = true;
        return toolTextResult(err, true);
    }
    return toolJsonResult({{"ok", true}, {"id", id}, {"sourcePath", services_.plugins().sourcePath(id)}});
}

QJsonObject McpServerCore::toolBuildCustomPlugin(const QJsonObject& args, bool* isError) {
    const QString id = args.value("id").toString();
    if (id.isEmpty()) {
        *isError = true;
        return toolTextResult("Missing required argument: id", true);
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    PluginBuildResult buildRes;
    bool done = false;
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    services_.plugins().buildPlugin(id, this, [&](PluginBuildResult r) {
        buildRes = std::move(r);
        done = true;
        loop.quit();
    });
    timer.start(60000);
    if (!done) loop.exec();

    if (!done) {
        *isError = true;
        return toolTextResult(QString("Build timed out for plugin: %1").arg(id), true);
    }

    QJsonArray diags;
    for (const auto& d : buildRes.diagnostics) {
        diags.append(QJsonObject{{"file", d.file}, {"line", d.line}, {"column", d.column},
                                 {"severity", d.severity}, {"message", d.message}});
    }
    *isError = !buildRes.ok;
    return toolJsonResult({{"ok", buildRes.ok},
                           {"id", id},
                           {"module", buildRes.module},
                           {"error", buildRes.error},
                           {"output", buildRes.output},
                           {"diagnostics", diags}},
                          !buildRes.ok);
}

QJsonObject McpServerCore::toolExportPipelineSo(Controller* controller, const QJsonObject& args, bool* isError) {
    PipelineExportOptions opts;
    opts.moduleName = args.value("moduleName").toString();
    opts.outputDir = args.value("outputDir").toString();
    if (args.contains("version")) opts.version = args.value("version").toString();
    if (args.contains("description")) opts.description = args.value("description").toString();

    const PipelineExportResult res = PipelineCompiler::compile(controller->plan(), opts, &services_.plugins());
    *isError = !res.ok;
    return toolJsonResult({{"ok", res.ok},
                           {"soPath", res.soPath},
                           {"integrationDocPath", res.integrationDocPath},
                           {"exampleHostPath", res.exampleHostPath},
                           {"output", res.output},
                           {"error", res.error}},
                          !res.ok);
}

QJsonObject McpServerCore::toolGenerateHostIntegrationPrompt(Controller* controller, const QJsonObject& args) {
    const QString moduleName = args.value("moduleName").toString("pipeline_engine");
    const QString audience = args.value("targetAudience").toString("Host UI Team");
    const QString extra = args.value("extraNotes").toString();

    QStringList paramLines;
    for (const auto& ex : controller->plan().exposed) {
        paramLines.append(QString("- `%1` -> `%2.%3`").arg(fromStd(ex.name), fromStd(ex.element), fromStd(ex.property)));
    }
    if (paramLines.isEmpty()) paramLines.append("- (No exposed parameters yet — use `edit_pipeline` with `expose_param`)");

    QString prompt = QString(
                         "# Host UI Integration Handoff: `%1`\n\n"
                         "**Target Audience:** %2\n\n"
                         "## 1. Runtime Loading (`runtime_sdk/`)\n"
                         "Load the compiled `.so` using `gps::runtime::Pipeline` (`gps_pipeline_get_module_v1`):\n"
                         "```cpp\n"
                         "gps::runtime::Pipeline pipe;\n"
                         "pipe.open(\"./lib%1.so\");\n"
                         "pipe.start();\n"
                         "```\n\n"
                         "## 2. Exposed Runtime Parameters (`setParam` / `getParam`)\n"
                         "%3\n\n"
                         "## 3. Telemetry & Lifecycle\n"
                         "- Call `pipe.metrics()` on a UI timer for lock-free per-element latency and drop-rate telemetry.\n"
                         "- Call `pipe.pause()`, `pipe.stop()` cleanly on window/session teardown.\n")
                         .arg(moduleName, audience, paramLines.join('\n'));
    if (!extra.isEmpty()) prompt += QString("\n## 4. Additional Notes\n%1\n").arg(extra);

    return toolTextResult(prompt);
}
