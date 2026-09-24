#!/usr/bin/env bash
# Cross-compile wyd_client for aarch64 inside an amd64 Ubuntu container.
# Leaves client748/project (x86_64) untouched; writes client748/project-aarch64.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${WYD_AARCH64_IMAGE:-ubuntu:24.04}"

docker run --rm --platform linux/amd64 \
  -v "$ROOT:/src:rw" \
  -w /src \
  -e DEBIAN_FRONTEND=noninteractive \
  -e DXVK_VER="${WYD_DXVK_NATIVE_VER:-3.0.2}" \
  "$IMAGE" \
  bash -lc '
set -euxo pipefail
dpkg --add-architecture arm64
cat > /etc/apt/sources.list <<EOF
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-updates main restricted universe multiverse
deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-security main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse
EOF
rm -f /etc/apt/sources.list.d/*
apt-get update -qq
apt-get install -y -qq \
  build-essential g++ cmake pkg-config git curl ca-certificates file \
  meson ninja-build glslang-tools python3 \
  spirv-headers libvulkan-dev \
  g++-aarch64-linux-gnu crossbuild-essential-arm64 \
  libsdl2-dev:arm64 libcurl4-openssl-dev:arm64 \
  libwayland-dev:arm64 libvulkan-dev:arm64 libxkbcommon-dev:arm64 \
  wayland-protocols

export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig

DXVK_PREFIX=/src/third_party/dxvk-native-aarch64
DXVK_SRC=/tmp/dxvk-src

if [[ ! -e "$DXVK_PREFIX/usr/lib/libdxvk_d3d9.so" ]]; then
  echo "[dxvk] building v$DXVK_VER for aarch64..."
  rm -rf "$DXVK_SRC"
  git clone --depth 1 --branch "v$DXVK_VER" --recurse-submodules \
    https://github.com/doitsujin/dxvk.git "$DXVK_SRC"
  cd "$DXVK_SRC"
  cat > /tmp/aarch64.ini <<EOF
[binaries]
c = '\''aarch64-linux-gnu-gcc'\''
cpp = '\''aarch64-linux-gnu-g++'\''
ar = '\''aarch64-linux-gnu-ar'\''
strip = '\''aarch64-linux-gnu-strip'\''
pkg-config = '\''pkg-config'\''

[host_machine]
system = '\''linux'\''
cpu_family = '\''aarch64'\''
cpu = '\''aarch64'\''
endian = '\''little'\''

[properties]
pkg_config_libdir = '\''/usr/lib/aarch64-linux-gnu/pkgconfig'\''
EOF
  meson setup --cross-file=/tmp/aarch64.ini \
    --prefix="$DXVK_PREFIX/usr" --libdir=lib --buildtype=release \
    -Denable_d3d9=true -Denable_d3d10=false -Denable_d3d11=false \
    build-aarch64
  ninja -C build-aarch64 -j"$(nproc)"
  ninja -C build-aarch64 install
  mkdir -p "$DXVK_PREFIX/usr/include/dxvk"
  cp -a /src/third_party/dxvk-native/usr/include/dxvk/. "$DXVK_PREFIX/usr/include/dxvk/"
  cd "$DXVK_PREFIX/usr/lib"
  if [[ ! -e libdxvk_d3d9.so ]]; then
    ln -sf "$(ls -1 libdxvk_d3d9.so.* | head -1)" libdxvk_d3d9.so
  fi
  file libdxvk_d3d9.so*
else
  echo "[dxvk] reuse $DXVK_PREFIX"
fi

cd /src
rm -rf build-aarch64
cat > /tmp/aarch64-wyd.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "/usr/lib/aarch64-linux-gnu/pkgconfig")
set(PKG_CONFIG_EXECUTABLE /usr/bin/pkg-config CACHE FILEPATH "")
EOF

cmake -S . -B build-aarch64 \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/aarch64-wyd.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWYD_BUILD_REAL_CLIENT=ON \
  -DWYD_BUILD_SMOKE_PROJECT=OFF \
  -DWYD_BUILD_ARCH_TESTS=OFF \
  -DWYD_BUILD_NET_SMOKE=OFF \
  -DWYD_DXVK_NATIVE_ROOT="$DXVK_PREFIX"

cmake --build build-aarch64 --target wyd_client -j"$(nproc)"

# Ensure the documented path is always refreshed. The CMake target uses the
# human-readable OUTPUT_NAME "WYD Arm64" in cross builds.
if [[ -x "/src/client748/WYD Arm64" ]]; then
  cp -f "/src/client748/WYD Arm64" /src/client748/project-aarch64
elif [[ -x /src/build-aarch64/wyd_client ]]; then
  cp -f /src/build-aarch64/wyd_client /src/client748/project-aarch64
fi
echo "==== RESULT ===="
file /src/client748/project-aarch64
file /src/client748/project
ls -la /src/client748/project /src/client748/project-aarch64
'
