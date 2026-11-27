# GstPipeStudio repository instructions

Read [Product Vision](docs/PRODUCT_VISION.md), [Coding Style](docs/development/coding-style.md) and the [architecture](docs/architecture/README.md) before changing code. The style applies to every change in this repository.

- Respect the 7-unit dependency DAG: `Pipeline` (pure C++17, no Qt/GStreamer), `ElementSDK` (`element_sdk/`, no Qt/`gstpipestudio`), `RuntimeSDK` (`runtime_sdk/`, standalone host C/C++ `.so` loader, no Qt/`gstpipestudio`), `GstEngine` (`Pipeline` + GStreamer, no Qt), `Application` (`Pipeline` + Qt Core/Gui, no `QtWidgets` or direct GStreamer), `MCP` (`gps-mcp` + `Application/mcp/`), and `GUI` (`Application` + Qt Widgets).
- Identify the class that owns the affected rule before editing. Search its callers across the GUI, CLI tools, MCP server and GStreamer plugin. Extend the owner and migrate callers when the rule is scattered.
- Keep dependency wiring at the composition root (`AppServices` / `GstPipeStudio` / CLI `main`). Reuse existing extension contracts (`IAction`, `IExecutionBackend`, `SaveTarget`, `ComponentProvider`) before introducing new ones.
- Add feature variants through an existing behavior contract or one registration at its owner. Use guards for local invariants and errors; route user-facing errors through `Notice` / `DiagnosticLog` instead of silent failure.
- Pass caller-adjustable values through typed options or object constructors. Parse external settings at the composition boundary and keep each default with its option.
- Verify every change in the DeepStream development image from the repository root:

  ```sh
  docker/dev.sh deepstream 'cmake --build build-docker-deepstream -j && cd build-docker-deepstream && ctest --output-on-failure'
  ```

- Record build failures and skipped tests distinctly. Hardware support requires a test on the relevant hardware.
