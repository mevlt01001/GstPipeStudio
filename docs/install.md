# Install and first run

**New to Docker? Choose the .deb first.** It integrates with your desktop without configuring display forwarding.

## Ubuntu desktop (.deb)

1. Open [Releases](https://github.com/mevlt01001/GstPipeStudio/releases). Current releases are marked **pre-release**, so use this list rather than GitHub's `/releases/latest` shortcut.
2. Choose your Ubuntu version (20.04, 22.04 or 24.04) and processor: `x86_64` for most PCs; `aarch64` for ARM64. `uname -m` prints your processor type; `lsb_release -rs` prints Ubuntu's version.
3. Download the `.deb` and matching `.deb.sha256` into the same directory. In that directory:

   ```bash
   sha256sum -c GstPipeStudio-22.04-x86_64.deb.sha256
   sudo apt install ./GstPipeStudio-22.04-x86_64.deb
   ```

   Replace both filenames with your download. Stop if the checksum fails.
4. Open **GstPipeStudio** from the application menu or run `gstpipestudio`.
5. Choose **Add source → videotestsrc**, then add `videoconvert` and `fakesink`. Validate, then Run. No camera or GPU is required.

For live charts, download/clone the [repository](https://github.com/mevlt01001/GstPipeStudio) and open `examples/monitoring/02-slow-stage.conf`, then **View → Live metrics → Start monitoring**. The third example deliberately triggers QoS-reported drops.

For additional plugins: `sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav`. NVIDIA/DeepStream requires its own compatible driver/SDK; ARM packages alone do not prove device support.

## Docker: what you download

An **image** contains the app and its libraries. A **container** is a running copy of that image. Docker must be installed on your host first. Follow [Docker's Ubuntu installation guide](https://docs.docker.com/engine/install/ubuntu/) for currently supported host releases, then verify `docker run --rm hello-world`. On an older host such as Ubuntu 20.04, check Docker's current support list; the native .deb is the simpler option.

If Docker reports permission denied, follow the official [post-install guide](https://docs.docker.com/engine/install/linux-postinstall/). Docker-group membership grants elevated host access; do not use `sudo` for the GUI launcher below.

The app image's Ubuntu version does **not** have to match the host's Ubuntu version: Docker supplies the container user space, while the host provides the kernel. CPU architecture must match. The existing public `0.3.0` and `latest` images are **Ubuntu 22.04 / amd64**; no ARM runtime image is currently promised.

For a download-only check (no login required for the public image):

```bash
docker pull ghcr.io/mevlt01001/gstpipestudio:0.3.0
docker run --rm -e QT_QPA_PLATFORM=offscreen ghcr.io/mevlt01001/gstpipestudio:0.3.0 --version
```

### Open the GUI on Linux

Requires a local X11 desktop or Wayland with XWayland, `DISPLAY`, and `xauth` (`sudo apt install xauth`). Native macOS/Windows Docker Desktop GUI forwarding is not covered.

```bash
git clone https://github.com/mevlt01001/GstPipeStudio.git
cd GstPipeStudio
./docker/run.sh examples/monitoring/02-slow-stage.conf
```

The launcher downloads the image, shares **only the current folder** at `/work`, passes a temporary display-authentication cookie and runs as your user. Save under `/work` to keep files after closing the container; preferences stored inside the container are temporary. Run it from a different directory to share that directory instead. No `xhost +` or privileged container is required. The script reports missing Docker/display prerequisites before launch.

`GPS_DOCKER_IMAGE=ghcr.io/mevlt01001/gstpipestudio:0.3.0 ./docker/run.sh` selects a specific image. If `DISPLAY` or its authentication cookie is unavailable, launch from a terminal in the desktop session or use the .deb.

The basic launcher does not pass through cameras, audio or GPUs. Those need device/driver-specific setup; test the synthetic examples first and report hardware results separately. A container is not a compatibility guarantee for Jetson or DeepStream.

### Container versions and development

The next non-draft release workflow builds amd64 runtime tags `ubuntu20.04`, `ubuntu22.04`, `ubuntu24.04` and `<version>-ubuntu<release>`. **These additional tags are not available in 0.3.0.** They become usable only after that workflow publishes them. `latest` and `<version>` remain aliases for the Ubuntu 22.04 image. Draft release runs do not replace public images.

The `ghcr.io/mevlt01001/gstpipestudio-dev` image is a separate Ubuntu 22.04 C++/Qt build environment, not the GUI application. See [development commands](development/README.md) for building. Docker images carry GStreamer and Qt; they do not require the host to install those development packages.

## Troubleshooting

| Symptom | First check |
|---|---|
| Wrong version or build | Help → About shows channel, commit and executable path. Development builds have a separate menu entry/settings. |
| Plugin missing | Run `gst-inspect-1.0 element_name` in the same environment as the app. Container and host plugin sets differ. |
| File disappears after Docker closes | Save in `/work`, not the container's temporary home. |
| Cannot open display | Use a local desktop session with XWayland/X11 and `xauth`; use the launcher rather than disabling display access control. |
| Device pipeline fails | Try `videotestsrc` first; then submit the hardware report with driver/SDK and pipeline details. |

[Get help](../SUPPORT.md) · [Contribute](../CONTRIBUTING.md)

For a contributor shell with source mounted read-only and build output kept in a Docker volume:

```bash
docker pull ghcr.io/mevlt01001/gstpipestudio-dev:0.3.0
docker run --rm -it -v "$PWD:/workspace:ro" -v gstpipestudio-build:/build ghcr.io/mevlt01001/gstpipestudio-dev:0.3.0
# Inside the container:
cmake -S /workspace/gstpipestudio -B /build -G Ninja
cmake --build /build -j4
ctest --test-dir /build --output-on-failure
```

The named volume avoids creating root-owned build files in your checkout. This is a build/test shell; use the runtime image for the GUI.
