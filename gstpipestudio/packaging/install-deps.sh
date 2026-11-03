#!/usr/bin/env bash
# Installs what building, testing and packaging GstPipeStudio needs on Ubuntu 20.04, 22.04 or 24.04 (run as root).
# The list is the build dependency list of the .deb; the run-time dependencies are worked out from the binaries.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git ca-certificates python3 patchelf dpkg-dev file binutils \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libglib2.0-dev \
    gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    libgl1-mesa-dev libegl1 libxkbcommon0 libxkbcommon-x11-0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xkb1 libxcb-xinerama0 libxcb-cursor0 \
    libwayland-client0 libwayland-cursor0 libwayland-egl1 libfontconfig1 libdbus-1-3 fonts-dejavu-core
