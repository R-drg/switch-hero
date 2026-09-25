#!/usr/bin/env bash
# Builds the browser version into build-web/. Needs the Emscripten SDK
# (https://emscripten.org/docs/getting_started/downloads.html) on PATH, e.g.
# after `source ./emsdk_env.sh`.
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! command -v emcmake >/dev/null; then
  echo 'Install and activate the Emscripten SDK first; see README.md.' >&2
  exit 1
fi
emcmake cmake -S "$project_root" -B "$project_root/build-web" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_root/build-web" --parallel 4
echo "Built: $project_root/build-web/switch-hero.html"
echo "Play: python3 tools/serve-web.py, then open http://localhost:8000/switch-hero.html"
