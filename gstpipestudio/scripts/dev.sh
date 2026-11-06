#!/usr/bin/env bash
# Repeatable local build/test entrypoint; does not install system dependencies.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${1:-full}"
case "$MODE" in
    core) DIRS='Pipeline;tests'; TEST_DIRS=unit ;;
    full) DIRS='Pipeline;GstEngine;tools;plugin;GUI;packaging;tests'; TEST_DIRS='unit;engine;cli;plugin;gui;sweep;deepstream' ;;
    *) echo 'Usage: dev.sh [core|full]' >&2; exit 2 ;;
esac
BUILD_DIR="${GPS_BUILD_DIR:-$ROOT/build-dev-zero-$MODE}"
cmake -S "$ROOT/gstpipestudio" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug -DGPS_DIRS="$DIRS" -DGPS_TEST_DIRS="$TEST_DIRS" \
    -DGPS_SANITIZERS="${GPS_SANITIZERS:-OFF}"
cmake --build "$BUILD_DIR" --parallel "${GPS_JOBS:-4}"
(cd "$BUILD_DIR" && ctest --output-on-failure)
