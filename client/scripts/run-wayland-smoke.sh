#!/usr/bin/env bash
# Smoke a partir de client748/ (binário instalado + assets locais).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/client748"

if [[ ! -x "${DEST}/project" ]]; then
  "${ROOT}/scripts/build.sh"
fi

export SDL_VIDEODRIVER=wayland
export VK_LOADER_LAYERS_DISABLE="${VK_LOADER_LAYERS_DISABLE:-*steam*}"
# Assets = pasta do executável (não precisa WYD_ASSET_ROOT).
unset WYD_ASSET_ROOT || true

if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "WAYLAND_DISPLAY não definido — este port exige Wayland." >&2
  exit 1
fi

cd "${DEST}"
echo "== client748/project (asset root = $(pwd)) =="
if [[ -n "${WYD_SMOKE_SECONDS:-}" ]]; then
  timeout --foreground "${WYD_SMOKE_SECONDS}" ./project || {
    code=$?
    [[ $code -eq 124 ]] && exit 0
    exit $code
  }
else
  ./project
fi
