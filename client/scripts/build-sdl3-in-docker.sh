#!/usr/bin/env bash
# Build SDL3 in Docker for WYDLINUX SDL3 port.
# Usage: bash scripts/build-sdl3-in-docker.sh
set -euo pipefail

PROJECT_ROOT="$(cd "$PWD" && pwd)"
WORKSPACE="/workspace"
IMAGE="wyd-sdl3-builder"
CONTAINER="wyd-sdl3-$(date +%s)"

CMAKE_OPTS=(
  -S /src/sdl
  -B /src/sdl/build
  -DSDL_SHARED=ON
  -DSDL_STATIC=OFF
  -DSDL_TESTS=OFF
  -DSDL_EXAMPLES=OFF
  -DCMAKE_INSTALL_PREFIX=/opt/sdl3
  -DCMAKE_BUILD_TYPE=Release
)

docker build -t "$IMAGE" - <<'EOF'
FROM ubuntu:noble
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config ninja-build git \
      libwayland-dev libxkbcommon-dev libegl1-mesa-dev \
      libgl1-mesa-dev libgles2-mesa-dev libdrm-dev \
      libasound2-dev libpulse-dev libdbus-1-dev libusb-1.0-0-dev \
      libudev-dev libffi-dev ninja-build && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /src
EOF

mkdir -p build/sdl3

mkdir -p build/sdl3

docker run --rm --name "$CONTAINER" \
  -v "$PROJECT_ROOT/third_party:/src/third_party:ro" \
  -v "$PROJECT_ROOT/build/sdl3:/src/out" \
  -w /src \
  "$IMAGE" bash -lc '
set -euo pipefail

if [ -d /src/third_party/SDL ]; then
  echo "[sdl3] using vendored SDL in third_party/SDL"
  SDL_DIR=/src/third_party/SDL
else
  echo "[sdl3] cloning SDL3"
  git clone --depth 1 --branch SDL3 https://github.com/libsdl-org/SDL.git /src/sdl
  SDL_DIR=/src/sdl
fi

echo "[sdl3] SDL dir: $SDL_DIR"
ls -la "$SDL_DIR"

cmake \
  -S "$SDL_DIR" \
  -B /src/out/build \
  -DSDL_SHARED=ON \
  -DSDL_STATIC=OFF \
  -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX=/src/out/install \
  -DCMAKE_BUILD_TYPE=Release \
  -G Ninja
cmake --build /src/out/build -j$(nproc)
cmake --install /src/out/build
echo "[sdl3] installed to /src/out/install"
'

echo
echo "SDL3 built into build/sdl3/install"
ls -la build/sdl3/install
