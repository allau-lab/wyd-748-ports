#!/usr/bin/env bash
# Build do client WYD 7.48 para Switch no Docker (devkitpro/devkita64).
# Compila SDL3 (switch-sdl-3.4) no container e linka TMProject748 + D3D9→GLES.
#
# Uso:
#   ./scripts/build-switch-docker.sh              # client real → OUT/switch/wyd748.nro
#   ./scripts/build-switch-docker.sh bringup       # smoke GLES (nao e o jogo)
#   ./scripts/build-switch-docker.sh clean
set -euo pipefail

IMAGE="${DEVKITPRO_IMAGE:-devkitpro/devkita64:latest}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-switch}"
CMD="${1:-all}"

command -v docker >/dev/null 2>&1 || { echo "erro: docker nao encontrado" >&2; exit 1; }

docker_bin=docker
if ! docker info >/dev/null 2>&1; then
  if command -v pkexec >/dev/null 2>&1; then
    echo "[WYD748] docker precisa de permissao — pkexec vai pedir a senha"
    docker_bin="pkexec docker"
  else
    echo "erro: sem acesso ao docker daemon" >&2
    exit 1
  fi
fi

if ! $docker_bin image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "[WYD748] puxando $IMAGE …"
  $docker_bin pull "$IMAGE"
fi

case "$CMD" in
  clean) rm -rf "$ROOT/$BUILD_DIR"; exit 0 ;;
  reconfigure)
    rm -rf "$ROOT/$BUILD_DIR"
    CLIENT_FLAGS="-DWYD_SWITCH_CLIENT_LINK=ON -DWYD_SWITCH_BRINGUP=OFF"
    ;;
  bringup)
    CLIENT_FLAGS="-DWYD_SWITCH_CLIENT_LINK=OFF -DWYD_SWITCH_BRINGUP=ON"
    ;;
  all|"")
    CLIENT_FLAGS="-DWYD_SWITCH_CLIENT_LINK=ON -DWYD_SWITCH_BRINGUP=OFF"
    ;;
  *) echo "uso: $0 [all|bringup|clean|reconfigure]" >&2; exit 2 ;;
esac

run_in() {
  $docker_bin run --rm \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e DEVKITPRO=/opt/devkitpro \
    -e DEVKITA64=/opt/devkitpro/devkitA64 \
    -v "$ROOT":/work \
    -w /work \
    "$IMAGE" bash -lc "$1"
}

run_in '
set -e
# Persistir SDL3 no tree montado (sobrevive ao container)
SDL3_PREFIX=/work/third_party/sdl3-switch
if [ ! -f "$SDL3_PREFIX/include/SDL3/SDL.h" ]; then
  echo "[WYD748] compilando SDL3 switch-sdl-3.4 → third_party/sdl3-switch …"
  rm -rf /tmp/wyd-sdl3-switch-src /tmp/wyd-sdl3-switch-build
  mkdir -p /work/third_party
  git clone --depth 1 --branch switch-sdl-3.4 \
    https://github.com/devkitPro/SDL.git /tmp/wyd-sdl3-switch-src
  cmake -S /tmp/wyd-sdl3-switch-src -B /tmp/wyd-sdl3-switch-build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_INSTALL=ON \
    -DCMAKE_INSTALL_PREFIX="$SDL3_PREFIX"
  cmake --build /tmp/wyd-sdl3-switch-build -j"$(nproc)"
  cmake --install /tmp/wyd-sdl3-switch-build
else
  echo "[WYD748] SDL3 Switch ja em $SDL3_PREFIX"
fi

cmake -S switch -B '"$BUILD_DIR"' \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
  -DSWITCH_SDL3_PREFIX="$SDL3_PREFIX" \
  -DWYD_OUT_DIR=/work/OUT/switch \
  '"$CLIENT_FLAGS"'
cmake --build '"$BUILD_DIR"' -j"$(nproc)"
'

mkdir -p "$ROOT/OUT/switch"
{
  echo "data:  $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "image: $IMAGE"
  echo "cmd:   $CMD"
} > "$ROOT/OUT/switch/build-info.txt"

if [[ -f "$ROOT/OUT/switch/wyd748.nro" ]]; then
  ls -lh "$ROOT/OUT/switch/wyd748.nro" "$ROOT/OUT/switch/wyd748.elf" 2>/dev/null || true
  echo "[WYD748] OK — OUT/switch/wyd748.nro (CLIENT REAL)"
  echo "[WYD748] Assets em sdmc:/switch/client748/ (config.txt, UI/, …)"
  echo "[WYD748] FTP (quando ligado): ftp://192.168.1.6:5000/switch/client748/"
else
  ls -lh "$ROOT/OUT/switch/" || true
  echo "[WYD748] aviso: wyd748.nro nao gerado — veja log do cmake acima" >&2
  exit 1
fi
