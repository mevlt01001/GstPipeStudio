# Contributing

Start with the [architecture](docs/architecture/README.md) and [build/test guide](docs/development/README.md).

Pick one observable problem and describe the expected behavior. Keep each change small enough to review. Use the same process whether writing code manually or with assistance; the contributor is responsible for understanding and verifying the change.

- **Pipeline** owns document edits, identities, connections and undo/redo. New actions use GraphEditor so failures are atomic. Keep it independent of Qt and GStreamer.
- **GUI** renders the document and invokes actions. Keep native GStreamer objects out of widgets and preserve user positions during runtime updates.
- **GstEngine** owns native resources, binding, cancellation and observations. Work from snapshots; return values tagged to their run, never mutate the GUI document.

Avoid duplicating state or editing rules. Add an interface when multiple implementations need the same contract; simple data records do not need inheritance. Explain ownership and callback lifetimes where they cross a boundary.

For a bug fix, add a focused regression when practical. For a behavior change, update the relevant test and documentation together. Record the actual commands/results in the PR, including skipped hardware checks and environments you could not exercise. A plugin discovered in the registry is not evidence that it runs on a GPU.

A PR should explain the user-visible problem, resulting behavior, affected contract/format and verification. Discuss file-format or architectural changes before widening their scope. Maintainer review is required before merging dev-zero into main.

## Where a change belongs

| Change | Owner | Verification |
|---|---|---|
| Graph operation, connections, undo | `Pipeline` / GraphEditor | unit tests |
| Native elements, runtime metrics | `GstEngine` | engine tests |
| Window, dialog, shared input behavior | `GUI/ui` | GUI tests |
| Pads, elements, connection visuals | `GUI/canvas` | GUI tests and visual review |

Keep animation state in the visual object that owns it; document edits go through Controller and Pipeline transactions. A new input rule belongs in a shared component, not copied event handlers. Use `.clang-format` and `.editorconfig`; avoid reformatting unrelated files. This applies equally to human and AI-authored changes.

Follow [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) for ownership and RAII, and [Qt object ownership](https://doc.qt.io/qt-6/objecttrees.html) for widgets and animations. Prefer composition; introduce polymorphism when there are interchangeable implementations. Keep factory-specific behavior out of generic widgets.
