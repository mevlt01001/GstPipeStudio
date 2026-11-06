#!/usr/bin/env bash
# Linux X11/XWayland desktop launcher: narrow cookie access, user-owned saved files.
set -euo pipefail
if [[ "${1:-}" == --help ]]; then
    echo 'Usage: docker/run.sh [pipeline.conf]'; echo 'Run from the folder to share with the app. GPS_DOCKER_IMAGE overrides the image tag.'; exit 0
fi
command -v docker >/dev/null || { echo 'Install Docker first: https://docs.docker.com/engine/install/ubuntu/' >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo 'Docker is not accessible. See docs/install.md (Docker permissions).' >&2; exit 1; }
[[ -n "${DISPLAY:-}" && -d /tmp/.X11-unix ]] || { echo 'A local Linux X11/XWayland desktop is required. Use the .deb for other desktop setups.' >&2; exit 1; }
command -v xauth >/dev/null || { echo 'Install xauth: sudo apt install xauth' >&2; exit 1; }
STATE_DIR="$(mktemp -d)"
COOKIE_FILE="$STATE_DIR/xauth"
CID_FILE="$STATE_DIR/container.cid"
cleanup() {
    if [[ -s "$CID_FILE" ]]; then docker rm -f "$(cat "$CID_FILE")" >/dev/null 2>&1 || true; fi
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
: > "$COOKIE_FILE"
chmod 600 "$COOKIE_FILE"
COOKIE="$(xauth nlist "$DISPLAY" 2>/dev/null)"
[[ -n "$COOKIE" ]] || { echo 'No X11 cookie found. Run from a terminal in your desktop session; do not use xhost +.' >&2; exit 1; }
printf '%s\n' "$COOKIE" | sed 's/^..../ffff/' | xauth -f "$COOKIE_FILE" nmerge -
IMAGE="${GPS_DOCKER_IMAGE:-ghcr.io/mevlt01001/gstpipestudio:latest}"
docker pull "$IMAGE"
docker run --rm --init --cidfile "$CID_FILE" --user "$(id -u):$(id -g)" \
    --env DISPLAY --env QT_QPA_PLATFORM=xcb --env HOME=/tmp/gstpipestudio-home \
    --env XAUTHORITY=/tmp/gstpipestudio.xauth \
    --mount "type=bind,source=$COOKIE_FILE,target=/tmp/gstpipestudio.xauth,readonly" \
    --mount type=bind,source=/tmp/.X11-unix,target=/tmp/.X11-unix,readonly \
    --volume "$PWD:/work" --workdir /work "$IMAGE" "$@"
