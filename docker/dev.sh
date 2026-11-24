#!/usr/bin/env bash
# Builds and tests the checkout inside a development image, as the host user.
# Usage: docker/dev.sh [ubuntu|deepstream] [command ...]
#   No command: configure, build and run every test in build-docker-<variant>/ (git-ignored).
#   deepstream: the container gets the GPUs, and the TensorRT engine cache of the DeepStream tests stays in the build folder.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VARIANT="${1:-ubuntu}"
case "$VARIANT" in
    ubuntu)     IMAGE=gstpipestudio-dev; GPU=() ;;
    deepstream) IMAGE=gstpipestudio-dev-deepstream; GPU=(--gpus all) ;;
    *) echo 'Usage: docker/dev.sh [ubuntu|deepstream] [command ...]' >&2; exit 2 ;;
esac
shift || true
docker image inspect "$IMAGE" >/dev/null 2>&1 || "$DIR/docker/build.sh" "$VARIANT"

BUILD="build-docker-$VARIANT"
DEFAULT="cmake -S gstpipestudio -B $BUILD -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build $BUILD && cd $BUILD && ctest --output-on-failure"
TTY=(); [[ -t 0 ]] && TTY=(-it)

docker run --rm "${TTY[@]}" "${GPU[@]}" --user "$(id -u):$(id -g)" \
    --env HOME=/tmp/home --env QT_QPA_PLATFORM=offscreen --env GPS_DS_CACHE="/workspace/$BUILD/ds-cache" \
    --volume "$DIR:/workspace" --workdir /workspace "$IMAGE" \
    bash -c 'mkdir -p "$HOME" && eval "$1"' _ "${*:-$DEFAULT}"
