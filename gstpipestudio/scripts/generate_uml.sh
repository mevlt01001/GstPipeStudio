#!/usr/bin/env bash
# Generates UML diagrams (Mermaid & PlantUML) from C++ headers/sources using clang-uml.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${1:-${ROOT}/build}"
OUT_DIR="${ROOT}/docs/uml"

mkdir -p "${OUT_DIR}"

if ! command -v clang-uml >/dev/null 2>&1; then
    echo "Installing clang-uml..."
    sudo add-apt-repository -y ppa:bkryza/clang-uml
    sudo apt-get update -qq
    sudo apt-get install -y -qq clang-uml plantuml graphviz
fi

if [ ! -f "${BUILD_DIR}/compile_commands.json" ]; then
    cmake -S "${ROOT}/gstpipestudio" -B "${BUILD_DIR}" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

clang-uml -c "${ROOT}/.clang-uml" --query-driver g++ -g mermaid -g plantuml -p "${BUILD_DIR}" -o "${OUT_DIR}"
if command -v plantuml >/dev/null 2>&1; then
    plantuml -tsvg "${OUT_DIR}"/*.puml || true
fi
echo "UML diagrams generated in ${OUT_DIR}"

