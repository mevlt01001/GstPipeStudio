# GstPipeStudio — Product Vision, Scope & Modular Architecture

## 1. Identity & Core Purpose

**GstPipeStudio** is a **Universal Real-Time Stream Pipeline IDE, Custom Element Workstation, and Compiled Pipeline Runtime** built natively on GStreamer.

It is NOT limited to computer vision or AI inference. Any real-time streaming workload — video, audio, sensor/UDP telemetry, custom binary protocols, byte-streams, or GPU-accelerated DeepStream pipelines — can be designed visually or imported via `gst-launch`, extended with native C++17 custom elements (`element_sdk/`), observed live with lock-free latency/drop telemetry, exported as a **standalone compiled `.so` stream engine** controlled from any C/C++ host application (`runtime_sdk/`), and driven by an **audited AI Driver via MCP (`gps-mcp`)**.

---

## 2. Explicit Scope & Non-Goals (Commercial & Engineering Boundaries)

Defining clear boundaries keeps the product sharp, maintainable, and commercially viable:

| What GstPipeStudio **DOES** | What GstPipeStudio **DOES NOT** Do (Explicit Non-Goals) |
| :--- | :--- |
| Designs, validates, and debugs arbitrary `M -> N` real-time GStreamer / DeepStream pipelines. | Does **not** build domain-specific end-user desktop/web UIs (e.g., a broadcaster studio app or hospital dashboard). |
| Builds native C++17 `GstElement` `.so` plugins (`element_sdk/`) with custom data contracts (`VideoFrame`, `AudioFrame`, `ByteStreamAdapter`, `CustomMeta<T>`, `NVMM`, `Mailbox` async side-inputs). | Does **not** train ML/GAN models. Instead, it wraps inference engines or custom processing nodes and exposes runtime controls. |
| Compiles any authored pipeline into a **dual-personality `.so`** (both a C/C++ `gps_pipeline_get_module_v1` module for `runtime_sdk/` and a native `GstBin` GStreamer plugin) + generates a **Host UI Team Integration Prompt** (`INTEGRATION.md`). | Does **not** embed a monolithic remote-SSH execution daemon in v0.5. Remote GPU offload is modeled explicitly via network/streaming elements (UDP/RTP/RTSP/SRT/TCP/custom `element_sdk` sender & receiver nodes). |
| Provides an **audited MCP server (`gps-mcp`)** with a live GUI **MCP Inspector** so external LLM drivers (Claude, Codex, Cursor, Antigravity) can inspect, build, validate, and export pipelines transparently. | Does **not** execute silent, unlogged AI mutations. Every MCP request and response is persistently audited and visible in real time. |

---

## 3. Clean 7-Unit Modular Architecture & Dependency DAG

```text
  GUI (Qt6 Widgets)  ──────────►  Application (Qt6 Core/Gui + Use Cases)  ◄──────  gps-mcp (Audited MCP Server)
                                          │                                                │
                                          ▼                                                ▼
                                   Pipeline (Pure C++17 Graph)  ◄──────────────  GstEngine (GStreamer Native)
                                                                                           ▲
                                   element_sdk (Standalone C++17 GstElement SDK) ──────────┤
                                   runtime_sdk (Standalone C/C++ Host .so Loader) ─────────┘ (consumes exported .so)
```

| Unit | Location | Responsibility | Allowed Dependencies |
| :--- | :--- | :--- | :--- |
| **1. `Pipeline`** | `gstpipestudio/Pipeline/` | Authored `Graph`, topology rules, undo/redo (`GraphEditor`), `.conf` codec, `TemplateInstance` semantics, structured `ElementDiagnostic`. | Pure C++17 only. **Zero Qt, zero GStreamer.** |
| **2. `ElementSDK`** | `element_sdk/` | Pure OOP C++17 `GstElement` custom plugin SDK (`Video`, `Audio`, `ByteStream`, `BufferList`, `BufferPool`, `DmaBuf`, `CustomMeta<T>`, `NVMM`, `Pull-mode`, `URI`, `Clock`). | GStreamer C APIs (+ optional DeepStream). **Zero Qt, zero `gstpipestudio`.** |
| **3. `RuntimeSDK`** | `runtime_sdk/` | Standalone ABI-stable C99/C++17 host library (`gps::runtime::Pipeline`) that loads compiled pipeline `.so` files (`open`, `start`, `pause`, `stop`, `setParam`, `pushFrame`, `setFrameCallback`, `metrics`). | Standard C++17 + `dlopen` (or GStreamer runtime). **Zero Qt, zero `gstpipestudio` headers.** |
| **4. `GstEngine`** | `gstpipestudio/GstEngine/` | Native GStreamer inspection, `Builder`, `Importer`, lock-free atomic `TelemetrySampler`, `Session`, and `gpsconf` plugin bridge. | `Pipeline`, GStreamer C APIs. **Zero Qt.** |
| **5. `Application`** | `gstpipestudio/Application/` | Document `Controller`, `BoxPlacement`, `DiagnosticLog`, `PluginLibrary`/`PluginToolchain`, `PipelineCompiler`, `FactoryCatalog`, `AppServices`, `McpAudit` & `McpProtocol`. | `Pipeline`, Qt6 Core/Gui, `quickjs`. **Zero `QtWidgets`, zero direct GStreamer calls.** |
| **6. `MCP` (`gps-mcp`)** | `gstpipestudio/tools/gps-mcp.cpp` + `Application/mcp/` | Audited `stdio` JSON-RPC 2.0 MCP server + `QLocalServer` live GUI bridge + self-describing driver manual (`gps://guide/driver-manual`). | `Application`, `Pipeline`. |
| **7. `GUI`** | `gstpipestudio/GUI/` | Canvas (`CanvasScene`), `Explorer`, `PluginWizardDialog`, `PluginEditorWindow`, `MCP` Inspector panel, `TelemetryPanel`, `OnboardingDialog`, `NotificationOverlay`. | `Application`, Qt6 Widgets. **Zero direct GStreamer calls.** |
