#!/usr/bin/env bash
# check_qt.sh – makes sure Qt 6.8.3 is in ~/Qt; it is built from its source on every system (it uses apt).
# One way on every Ubuntu and architecture: the ready-made Qt for ARM needs glibc 2.38 (Ubuntu 24.04), so it is not used.
set -euo pipefail

QT_VERSION=6.8.3
QT_HOME="${QT_HOME:-$HOME/Qt}"
QT_PREFIX="$QT_HOME/$QT_VERSION"
QT_DONE="$QT_PREFIX/.built"   # written last, only when the build and the install worked: no file, no (complete) Qt

# sudo, unless we are root already (a container usually is root and has no sudo)
SUDO=sudo
[ "$(id -u)" = 0 ] && SUDO=

# system libraries Qt needs to run and to be found by CMake; needed also when Qt comes from a cache, so outside the build
$SUDO apt-get update
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    ca-certificates libgl1-mesa-dev libegl1 libxkbcommon0 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
    libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xkb1 libxcb-xinerama0 libfontconfig1 \
    libdbus-1-3 fonts-dejavu-core

if [ ! -f "$QT_DONE" ]; then
    # only qtbase is built: it holds Core, Gui, Widgets and Test; the -dev packages are what the window (xcb) plugin needs
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential cmake ninja-build perl curl libfontconfig1-dev libfreetype6-dev libx11-dev libx11-xcb-dev \
        libxext-dev libxfixes-dev libxi-dev libxrender-dev libxkbcommon-dev libxkbcommon-x11-dev libxcb1-dev \
        libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev \
        libxcb-render-util0-dev libxcb-shape0-dev libxcb-shm0-dev libxcb-sync-dev libxcb-util-dev libxcb-xfixes0-dev \
        libxcb-xkb-dev
    SRC_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/gstpipestudio/qt-src"
    rm -rf "$SRC_DIR" "$QT_PREFIX"   # an unfinished earlier build and install
    mkdir -p "$SRC_DIR/build"
    curl -fL "https://download.qt.io/archive/qt/6.8/$QT_VERSION/submodules/qtbase-everywhere-src-$QT_VERSION.tar.xz" \
        | tar -xJ -C "$SRC_DIR"
    cd "$SRC_DIR/build"
    # -no-sbom: the software bill of materials of Qt needs CMake 3.19, and Ubuntu 20.04 has 3.16
    # -no-icu: otherwise Qt links the ICU of the machine when it finds one, and then Qt differs from system to system (and the
    #          package would need libicu66, libicu70 or libicu74); we show only technical names, no language rules are needed
    "../qtbase-everywhere-src-$QT_VERSION/configure" -prefix "$QT_PREFIX" -release -nomake examples -nomake tests -no-sbom -no-icu
    cmake --build . --parallel "$(nproc)"
    cmake --install .
    touch "$QT_DONE"
    cd /
    rm -rf "$SRC_DIR"   # the build folder is several GB
fi

# exit code of the script: 0 = Qt is built and installed, 1 = not
test -f "$QT_DONE"
echo "Qt $QT_VERSION is at $QT_PREFIX (give CMake: -DCMAKE_PREFIX_PATH=$QT_PREFIX)"
