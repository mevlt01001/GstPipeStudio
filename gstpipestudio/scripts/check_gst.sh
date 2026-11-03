#!/usr/bin/env bash
# check_gst.sh – makes sure GStreamer 1.16 or newer is on this system (it uses apt).

set -euo pipefail

MIN_VERSION=1.16

# sudo, unless we are root already (a container usually is root and has no sudo)
SUDO=sudo
[ "$(id -u)" = 0 ] && SUDO=


$SUDO apt-get update
if ! command -v gst-inspect-1.0 >/dev/null 2>&1; then
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl \
    gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio
fi

# "gst-inspect-1.0 version 1.20.3" is the first line of its answer
version="$(gst-inspect-1.0 --version | head -n1 | awk '{print $NF}')"

# exit code of the script: 0 = 1.16 or newer, 1 = older
dpkg --compare-versions "$version" ge "$MIN_VERSION"
1