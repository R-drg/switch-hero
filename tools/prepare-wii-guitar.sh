#!/usr/bin/env bash
# Prepare a pinned MissionControl source tree; no SD card or installed files are changed.
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
revision=d3941d433f15827de8aea116d61ea17bb61d0bcc
target="${1:-$project_root/build-missioncontrol}"
if [[ -e "$target" ]]; then
    echo "Destination already exists: $target. Choose a new directory." >&2
    exit 1
fi
git clone https://github.com/ndeadly/MissionControl.git "$target"
git -C "$target" checkout -b switch-hero-wii-guitar "$revision"
git -C "$target" submodule update --init --recursive
git -C "$target" apply --check "$project_root/integrations/missioncontrol/wii-guitar.patch"
git -C "$target" apply "$project_root/integrations/missioncontrol/wii-guitar.patch"
cp "$project_root/integrations/missioncontrol/wii_guitar.hpp" "$target/mc_mitm/source/controllers/"
echo "Prepared $target. Build with devkitPro: make -C \"$target\" dist"
