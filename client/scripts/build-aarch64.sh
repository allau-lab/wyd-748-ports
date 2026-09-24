#!/usr/bin/env bash
# Safe ARM64 client build: separate build tree + separate install path.
# Requires: aarch64-linux-gnu-g++, SDL2/curl/wayland/vulkan aarch64 packages,
# and DXVK Native built into third_party/dxvk-native-aarch64.
# Se o cross-compiler local não existir, usa scripts/build-aarch64-docker.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DXVK_ROOT="${WYD_DXVK_NATIVE_ROOT:-$ROOT/third_party/dxvk-native-aarch64}"
BUILD="$ROOT/build-aarch64"

if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
  echo "aarch64-linux-gnu-g++ ausente — usando build-aarch64-docker.sh" >&2
  exec "$ROOT/scripts/build-aarch64-docker.sh"
fi
if [[ ! -f "$DXVK_ROOT/usr/include/dxvk/d3d9.h" ]]; then
  echo "DXVK Native aarch64 not found at $DXVK_ROOT" >&2
  echo "Build DXVK Native for aarch64 and install prefix usr/ there first." >&2
  exit 1
fi

cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/aarch64-linux-gnu.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWYD_BUILD_REAL_CLIENT=ON \
  -DWYD_BUILD_SMOKE_PROJECT=OFF \
  -DWYD_DXVK_NATIVE_ROOT="$DXVK_ROOT"

cmake --build "$BUILD" --target wyd_client -j"$(nproc)"
echo "OK: $ROOT/client748/project-aarch64 (x86_64 client748/project unchanged)"
