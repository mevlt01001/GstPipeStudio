# GstPipeStudio C++17 and Qt coding style

## Responsibility and dependency direction

| Responsibility | Owner |
| --- | --- |
| Construct the desktop application, its services and its window | `GstPipeStudio` |
| Document use cases, validation and run coordination, persistence and component libraries | Application layer |
| Authored graph, topology rules, pad addresses, edits, undo/redo and template instance semantics | `Pipeline` |
| Native pipelines, properties, pad binding and telemetry | `GstEngine` |
| Widgets, selection and animation, dialogs, command wiring and error presentation | `GUI` |

- Apply encapsulation. A class that owns an invariant keeps its mutable state private and exposes operations in the vocabulary of the feature. Use value types for snapshots, addresses, reports and independent data fields.
- Give each rule one owner. Examples include graph validity, whether a document is dirty, which graph a run uses and how a template revision resolves. Other callers consume the owner's result. Keep syntax checks in codecs, domain checks in the model and error presentation in the GUI.
- Compose services at the application entry point and provide dependencies through constructors. Keep service lookup local to a typed collection of interchangeable providers. A document or widget receives the dependencies it uses.
- The application layer may use Qt Core and Qt Gui for integration. Qt Widgets belongs to the GUI. Pipeline stays independent of Qt and GStreamer. Native GStreamer objects stay in GstEngine and its native entry points.

## Interfaces and extension points

- Use an interface for a contract with interchangeable implementations or a boundary that must hide its implementation. Use a pure abstract C++ class and a virtual destructor for an owning polymorphic boundary. Mark overrides explicitly. Use an abstract base with shared state only when that state and lifecycle belong to every implementation.
- Add behavior through an existing contract where it fits. Use `IAction` for graph edits, `IExecutionBackend` for validation backends, `SaveTarget` for writable document destinations and `ComponentProvider` for placeable components. A new implementation should require registration at the composition point and focused tests.
- Keep templates first-class. Preserve revision, nested instance, parameter and boundary-pad semantics. Custom element providers use the same component discovery and placement path. A provider owns its specialized behavior and reports its supported operations.
- Treat a protocol or file format as a compatibility boundary. Verify previously saved files and normal entry points before changing it. A single in-memory rule may have different diagnostics at different boundaries.

## Branching and configuration

- Keep control flow small and local. A feature variant registers behavior once at its owner. Prefer an interface when variants own state or resources. Use a typed function table when variants differ by operations on values. One registration includes the variant's behavior and metadata, such as selection, defaults and comparison.
- Review branches introduced by a change regardless of syntax. Repeated checks for a concrete type, mode, capability or configuration value usually identify a decision that belongs with the selected behavior. Replace the repeated decision at its owner and route existing callers through it.
- Keep conditions where they express an invariant, an input guard, an error or an explicit transition in a closed state machine. Track branch counts as a review signal. A count alone cannot establish whether a condition is necessary.
- Locate settings at the owner of the behavior they influence. Expose meaningful choices through typed constructor options. Read environment variables, persisted preferences or command arguments in the relevant adapter. Validate values at that boundary and inject the result. Use private state for invariants, lifecycle and resource ownership.
- Use one persisted representation and one in-memory owner per concept. Delete a compatibility path when support for that version is retired. Identify affected formats and check file ownership before removal. A shared code path for current behavior should have tests for its normal and failure cases.

## Ownership and work across threads

- Use scoped values and RAII for files, processes and native references. Use a Qt parent for parent-owned objects or `std::unique_ptr` for exclusive C++ ownership. Use shared ownership only when multiple owners must independently keep the object alive. Raw pointers and references are borrowed.
- A queued callback has a context whose lifetime bounds the work. Guard borrowed objects that may disappear independently with `QPointer` or the appropriate weak reference. Send values or snapshots across threads. Tag results with the document revision or run ID, and reject stale results before changing state.
- Make graph edits atomic through GraphEditor. For multi-file writes, account for failure between files and report the resulting state. Keep the active authored graph separate from runtime observations and temporary execution snapshots.

## Formatting and review

- Follow the root `.clang-format` and `.editorconfig`. Use four-space indentation, a 140-column C++ limit, left-aligned pointer markers, `PascalCase` types, `camelCase` functions and variables, and a trailing underscore on private data members. Keep existing public naming contracts and GStreamer C entry points.
- Keep functions at one level of responsibility and name the states they handle. Prefer a scoped enum for a closed state set. Write comments for constraints such as pad identity or ownership and remove comments made stale by the change.
- Cover regressions at the smallest relevant boundary. Exercise a shared graph rule through each entry point that changed, and test async cancellation, stale results, or partial writes when the change touches those paths. Run the prescribed Docker build and test commands in [AGENTS.md](../../AGENTS.md).

## Changes to these rules

Propose a change to this style or the root repository instructions in a dedicated change. Explain the original constraint, the concrete design problem, the proposed rule, affected entry points and the evidence required to verify it. Obtain maintainer approval before applying the new rule to feature implementation.

## References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) rules C.2, C.9, C.120–C.129 and R.1–R.21.
- [Composition Root](https://blog.ploeh.dk/2011/07/28/CompositionRoot/) by Mark Seemann.
- [Qt Object Trees and Ownership](https://doc.qt.io/qt-6/objecttrees.html) and [Signals and Slots](https://doc.qt.io/qt-6/signalsandslots.html).
