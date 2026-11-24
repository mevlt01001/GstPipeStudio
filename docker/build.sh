#!/usr/bin/env bash
# Builds the development image. Usage: docker/build.sh [ubuntu|deepstream]
#   ubuntu      gstpipestudio-dev             plain Ubuntu 22.04, as in CI
#   deepstream  gstpipestudio-dev-deepstream  DEEPSTREAM_IMAGE (default DeepStream 6.4, Ubuntu 22.04) for the GPU tests
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "${1:-ubuntu}" in
    ubuntu)     BASE=ubuntu:22.04; IMAGE=gstpipestudio-dev ;;
    deepstream) BASE="${DEEPSTREAM_IMAGE:-nvcr.io/nvidia/deepstream:6.4-gc-triton-devel}"; IMAGE=gstpipestudio-dev-deepstream ;;
    *) echo 'Usage: docker/build.sh [ubuntu|deepstream]' >&2; exit 2 ;;
esac

echo "Building ${IMAGE} on ${BASE}..."
docker build --build-arg BASE="${BASE}" -t "${IMAGE}" -f "${DIR}/docker/Dockerfile" "${DIR}"
echo "Build and test inside it: docker/dev.sh $([[ ${IMAGE} == *deepstream ]] && echo deepstream || echo ubuntu)"
