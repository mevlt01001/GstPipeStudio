#!/usr/bin/env bash
# check_qt.sh – makes sure Qt 6.8.3 is in ~/Qt (it uses apt and aqtinstall; the Qt of Ubuntu is never used).
set -euo pipefail

QT_VERSION=6.8.3
QT_HOME="${QT_HOME:-$HOME/Qt}"

# sudo, unless we are root already (a container usually is root and has no sudo)
SUDO=sudo
[ "$(id -u)" = 0 ] && SUDO=

case "$(uname -m)" in
    x86_64)        AQT_HOST=linux;       AQT_ARCH=linux_gcc_64;    ARCH_DIR=gcc_64 ;;
    aarch64|arm64) AQT_HOST=linux_arm64; AQT_ARCH=linux_gcc_arm64; ARCH_DIR=gcc_arm64 ;;
    *) echo "no Qt download for $(uname -m)" >&2; exit 1 ;;
esac
QT_PREFIX="$QT_HOME/$QT_VERSION/$ARCH_DIR"

# system libraries: CMake needs the OpenGL files, and Qt needs the others to open a window
$SUDO apt-get update
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    python3 python3-venv ca-certificates libgl1-mesa-dev libegl1 libxkbcommon0 libxkbcommon-x11-0 libxcb-cursor0 \
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xkb1 \
    libxcb-xinerama0 libwayland-client0 libwayland-cursor0 libwayland-egl1 libfontconfig1 libdbus-1-3 fonts-dejavu-core

if [ ! -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    # aqtinstall needs Python 3.9 or newer; Ubuntu 20.04 has 3.8 and offers 3.9 next to it
    PYTHON=python3
    if ! $PYTHON -c 'import sys; sys.exit(sys.version_info < (3, 9))'; then
        $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y python3.9 python3.9-venv
        PYTHON=python3.9
    fi
    AQT_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/gstpipestudio/aqt"
    $PYTHON -m venv "$AQT_DIR"
    "$AQT_DIR/bin/pip" install --quiet --upgrade pip wheel   # the pip of older Ubuntu cannot take binary wheels
    "$AQT_DIR/bin/pip" install --quiet aqtinstall
    cd "$AQT_DIR"   # aqt writes aqtinstall.log into the folder it runs in
    "$AQT_DIR/bin/aqt" install-qt "$AQT_HOST" desktop "$QT_VERSION" "$AQT_ARCH" --outputdir "$QT_HOME"
fi

# exit code of the script: 0 = Qt Widgets and Test are there, 1 = not
test -f "$QT_PREFIX/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake"
test -f "$QT_PREFIX/lib/cmake/Qt6Test/Qt6TestConfig.cmake"
echo "Qt $QT_VERSION is at $QT_PREFIX (give CMake: -DCMAKE_PREFIX_PATH=$QT_PREFIX)"