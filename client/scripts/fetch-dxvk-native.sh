#!/usr/bin/env bash
# Baixa DXVK Native (libdxvk_d3d9.so) — Steam Runtime sniper build.
# Uso: ./scripts/fetch-dxvk-native.sh [destino]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/third_party/dxvk-native}"
VER="${WYD_DXVK_NATIVE_VER:-3.0.2}"
ARCHIVE="dxvk-native-${VER}-steamrt-sniper.tar.gz"
BASE="https://github.com/doitsujin/dxvk/releases/download/v${VER}"

mkdir -p "$DEST"
cd "$DEST"

echo "[fetch-dxvk-native] destino=$DEST"
echo "[fetch-dxvk-native] $BASE/$ARCHIVE"

if ! command -v curl >/dev/null 2>&1; then
  echo "curl necessário" >&2
  exit 1
fi

curl -fL -o "$ARCHIVE" "$BASE/$ARCHIVE" || {
  echo "[fetch-dxvk-native] download falhou (tente WYD_DXVK_NATIVE_VER=2.7.1)."
  echo "Build: git clone https://github.com/doitsujin/dxvk && ./package-native.sh ..."
  exit 1
}

tar -xzf "$ARCHIVE"
SO="$(find . -path './usr/lib/libdxvk_d3d9.so' | head -1 || true)"
if [[ -z "$SO" ]]; then
  SO="$(find . -name 'libdxvk_d3d9.so' | head -1 || true)"
fi
if [[ -z "$SO" ]]; then
  echo "[fetch-dxvk-native] archive sem libdxvk_d3d9.so"
  exit 2
fi

ABS="$(cd "$(dirname "$SO")" && pwd)/$(basename "$SO")"
echo "[fetch-dxvk-native] OK: $ABS"
echo "export WYD_DXVK_LIBDIR=$(dirname "$ABS")"
echo "export WYD_D3D9_SO=$ABS"
echo "export DXVK_WSI_DRIVER=SDL2"
echo "cd WYDLINUX && ./scripts/build.sh && cd client748 && ./dxvk_native_smoke"
