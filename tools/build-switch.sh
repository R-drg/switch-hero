#!/usr/bin/env bash
set -euo pipefail
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -f "$DEVKITPRO/cmake/Switch.cmake" ]]; then
  echo 'Install the official devkitPro toolchain first; see README.md.' >&2
  exit 1
fi
cmake -S "$project_root" -B "$project_root/build-switch" \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_root/build-switch" --parallel 4
echo "Built: $project_root/build-switch/fretboard.nro"
