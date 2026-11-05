#!/usr/bin/env bash
# Generates UML diagrams (Mermaid & PlantUML) from C++ headers/sources using clang-uml.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-${ROOT}/build}"
OUT_DIR="${ROOT}/gstpipestudio/docs/uml"

mkdir -p "${OUT_DIR}"

SUDO=sudo
[ "$(id -u)" = 0 ] && SUDO=

if ! command -v clang-uml >/dev/null 2>&1; then
    echo "Installing clang-uml..."
    echo "deb [trusted=yes] https://ppa.launchpadcontent.net/bkryza/clang-uml/ubuntu jammy main" | $SUDO tee /etc/apt/sources.list.d/clang-uml.list >/dev/null
    $SUDO apt-get update -qq
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq clang-uml plantuml graphviz
fi

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
    cmake -S "${ROOT}/gstpipestudio" -B "${BUILD_DIR}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

clang-uml -c "${ROOT}/.clang-uml" --query-driver g++ -g mermaid -g plantuml -d "${BUILD_DIR}" -o "${OUT_DIR}"
if command -v plantuml >/dev/null 2>&1; then
    plantuml -tsvg "${OUT_DIR}"/*.puml || true
fi
echo "UML diagrams generated in ${OUT_DIR}"

