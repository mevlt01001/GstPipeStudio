# dev-zero review — 2026-11-06

This is a local review build, not a published release. No merge to main or remote workflow dispatch was performed.

## What to review

The three units are GUI, Pipeline and GstEngine. Pipeline owns the committed document; GUI submits transactions/actions; native execution consumes an immutable snapshot. Connections use identities instead of shared mutable raw pointers. See the [implemented architecture and UML](../architecture/README.md).

1. Add `videotestsrc` with no sink. Verify preview reports its output caps without inserting synthetic nodes into the canvas.
2. Connect a filter and sink; change a property and connection caps. Verify the resulting observations.
3. Drag elements far left/up. Pan farther, undo and redo; preview should not reset their positions.
4. Select a connected fragment and insert it into another connection. An ambiguous or disconnected selection must be refused without losing the original edge.
5. Edit while a preview is in progress. The latest edit must win; playback must still block editing.
6. Save, reopen and compare the graph. Existing version-1 `.conf` files remain supported.

## Verification evidence

| Environment/check | Result |
|---|---|
| Ubuntu 22.04, Qt 6.8.3, GStreamer 1.20.3, full CTest | 21 passed, 4 skipped, 0 failed (25 registered) |
| Ubuntu 20.04, GStreamer 1.16.3, core/native/CLI/plugin suite | 12 passed, 1 skipped, 0 failed (13 registered) |
| AddressSanitizer + UndefinedBehaviorSanitizer, core binaries in container | Graph: 219 checks; Pipeline: 48 checks; no failures or sanitizer findings |
| Package construction | Ubuntu 22.04 amd64 `.deb` and SHA-256 generated with bundled Qt |
| Fresh Ubuntu 22.04 container installation | apt installation, factory inspection, `gpsconf` discovery and offscreen GUI startup passed |
| Extracted package, host offscreen startup | Stayed running until the smoke-test timeout; no startup error output |
| Whitespace and shell syntax | `git diff --check` and `bash -n` passed |

The GStreamer 1.16 run verifies the native compatibility baseline; the subsequent preview coalescing changes were exercised by the final Qt/full suite and core sanitizer run.

Four full-suite skips require unavailable NVIDIA/DeepStream components. No GPU/Jetson hardware claim is made. One pre-existing integration expectation remains marked XFAIL: request sink pad display ordering is newest-first. It is not fixed by this migration. The six-platform CI matrix was updated but has not been run remotely.

Local evidence for this session:

- `/tmp/gps-zero-build/test-full.log` and `Testing/Temporary/LastTest.log`
- `/tmp/gps-zero-116/test.log`
- `/tmp/gps-zero-build/report/screenshots/`
- `/tmp/gps-zero-build/package.log`
- `/tmp/gps-zero-build/install-smoke.log`, `installed-inspect.json`, `installed-plugin.txt`, `installed-gui.log`
- `/tmp/gps-zero-build/packages/GstPipeStudio-22.04-x86_64.deb`

These temporary paths are session artifacts, not files required by contributors. Use the [development commands](README.md) to reproduce the build.

## Launch without installing

The review package is extracted locally. Run:

```bash
/tmp/gps-zero-preview/usr/bin/gstpipestudio
```

This Debug build is for inspection. Review the UI and architecture before authorizing a merge; publishing is a separate step.
