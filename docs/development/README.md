# Development and review

Use C++17, CMake 3.16+, Qt 6.8 and GStreamer development packages 1.16+. Ninja is optional. The system setup scripts install dependencies; `dev.sh` only builds and tests. No agent-specific instruction files are required.

```bash
# Pure Pipeline, no Qt/GStreamer dependency:
gstpipestudio/scripts/dev.sh core

# Full application and tests (set CMAKE_PREFIX_PATH if Qt is not on the default path):
CMAKE_PREFIX_PATH=/path/to/Qt/6.8.3 gstpipestudio/scripts/dev.sh full
build-dev-zero-full/bin/gstpipestudio

# Same core suite with memory/undefined-behaviour checks:
GPS_BUILD_DIR="$PWD/build-dev-zero-asan" GPS_SANITIZERS=ON gstpipestudio/scripts/dev.sh core
```

For manual control:

```bash
cmake -S gstpipestudio -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3
cmake --build build --parallel 4
(cd build && ctest --output-on-failure)
```

`GPS_DIRS` can select `Pipeline;GstEngine;tools;plugin;GUI;packaging;tests`; invalid directory names fail configuration. `GPS_TEST_DIRS` selects test groups. `GPS_SWEEP_ALL=ON` opts into the long all-factory survey. Test screenshots/logs go under the build directory, so a read-only source checkout is supported.

| Change | Relevant checks |
|---|---|
| Graph, action, history, persistence | `ctest -L unit --output-on-failure` |
| Native binding, caps, runtime | `ctest -L engine --output-on-failure` |
| CLI/plugin contracts | `ctest -L 'cli|plugin' --output-on-failure` |
| Canvas/controller/presentation | `ctest -L gui --output-on-failure` |
| Cross-unit changes | Full suite, plus supported-minimum environment |

Run ctest from the build directory for CMake 3.16. Qt tests use the offscreen platform. DeepStream tests skip when their required environment is absent; a skip is not GPU validation.

For the live metrics walkthrough, capture the actual Qt window with the built helpers, then generate the README clips from its numbered frames. `ffmpeg` and the DejaVu Sans font are needed for video generation:

```bash
cmake --build build --target demo_capture
GPS_BIN_DIR="$PWD/build/bin" GPS_DEMO_DIR="$PWD/build/demo/frames" \
  QT_QPA_PLATFORM=offscreen build/bin/demo_capture
GPS_DEMO_DIR="$PWD/build/demo/frames" gstpipestudio/scripts/make_demos.sh
```

The generated clips under `docs/media` come from the running application and a standard GStreamer test source. The two monitoring examples can also be checked with `gps-monitor examples/monitoring/02-slow-stage.conf --timeout-ms 3000`.

The import boundary is `gps::importLaunch` in GstEngine. It reconstructs a Pipeline `Graph` from a `gst_parse_launch` result; GUI calls it through `gps-import` so a short probe cannot block the window. Test the importer with `ctest -R '^engine.import$|^gui.real_tools_test$' --output-on-failure`. It only imports elements and links that exist during the bounded probe. The app does not attempt cross-process pointer access.

## Build container

`docker/Dockerfile` builds the development image. Once available:

```bash
docker run --rm -v "$PWD:/workspace:ro" -v "$PWD/build-container:/build" \
  gstpipestudio-dev:latest bash -lc \
  'cmake -S /workspace/gstpipestudio -B /build -DCMAKE_BUILD_TYPE=Debug && cmake --build /build -j4 && cd /build && ctest --output-on-failure'
```

Use a separate build directory for each compiler/container. Reusing a host build inside a container can preserve incompatible absolute paths.

## Packages and release gate

```bash
cmake -S gstpipestudio -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3 -DCMAKE_INSTALL_PREFIX=/usr -DGPS_BUNDLE_QT=ON
cmake --build build --parallel 4
(cd build && ctest --output-on-failure)
DESTDIR=/tmp/stage cmake --install build
gstpipestudio/scripts/make_deb.sh 22.04 x86_64
```

`GPS_BUILD_DIR`, `GPS_STAGE_DIR` and `GPS_OUTPUT_DIR` override the package script paths. The package has a SHA-256 checksum. Test clean installation in a matching OS image before distribution; a staging smoke check alone does not prove dependency completeness.

The fast workflow runs on dev-zero pushes, pull requests and manual dispatch, and uploads packages/test screenshots. The six-platform matrix remains manual/reusable, including Ubuntu 20.04 for GStreamer 1.16. A release is manual, restricted to main and defaults to a draft. Changes in this branch do not publish a release.

Before merging: inspect the running UI, compare actions with the documented model, and review test evidence and limitations with the maintainer. Publishing remains a separate step after merge.

### Release notes

Open Actions → Release → Run workflow on main. Choose the version bump, paste the reviewed Markdown highlights into the required `release_notes` field, and keep `draft` enabled for review. The current release copy is in `docs/releases/next.md`; update it for each release. The workflow prepends these highlights to installation instructions and GitHub-generated notes. `.github/release.yml` groups merged PRs by label; direct commits may not receive useful prose automatically, so maintainers must include those changes in the highlights.

References: [GitHub generated notes](https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes) and [manual workflow inputs](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).

Development builds default to `GPS_BUILD_CHANNEL=Development`, which uses a separate application/settings identity. Published packages set `GPS_BUILD_CHANNEL=Release`. Pass `GPS_BUILD_REVISION` when Git is unavailable in the build container. About and support reports use this build metadata. Helpers are resolved beside the app (or via an explicit `GPS_BIN_DIR` test override), so launch the intended build directly instead of replacing `/usr/bin/gstpipestudio`.
Register a staged development bundle with `gstpipestudio/scripts/install-dev-launcher.sh /path/to/stage/usr/lib/gstpipestudio`. It creates **GstPipeStudio Development** and `~/.local/bin/gstpipestudio-dev`; the installed release remains a separate application.
