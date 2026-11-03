#!/usr/bin/env bash
# check_build_tools.sh – makes sure the tools that build GstPipeStudio are on this system (it uses apt).
set -euo pipefail

MIN_CMAKE=3.16

# sudo, unless we are root already (a container usually is root and has no sudo)
SUDO=sudo
[ "$(id -u)" = 0 ] && SUDO=

$SUDO apt-get update
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake ninja-build pkg-config python3

# "cmake version 3.22.1" is the first line of its answer
version="$(cmake --version | head -n1 | awk '{print $NF}')"

# exit code of the script: 0 = CMake 3.16 or newer, 1 = older
dpkg --compare-versions "$version" ge "$MIN_CMAKE"
