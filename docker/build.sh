#!/usr/bin/env bash
set -euo pipefail

# Build the development image
IMAGE_NAME="gstpipestudio-dev"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Building Docker image: ${IMAGE_NAME}..."
docker build -t "${IMAGE_NAME}" -f "${DIR}/docker/Dockerfile" "${DIR}"

echo ""
echo "To run an interactive shell inside the container:"
echo "docker run -it --rm -v \"${DIR}:/workspace\" ${IMAGE_NAME}"

