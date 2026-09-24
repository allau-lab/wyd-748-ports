# Cross-toolchain for GNU/Linux aarch64. Does not alter the default host build.
#
#   cmake -S . -B build-aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
#     -DWYD_DXVK_NATIVE_ROOT=$PWD/third_party/dxvk-native-aarch64
#   cmake --build build-aarch64 --target wyd_client -j$(nproc)
#
# OUTPUT_NAME "WYD Arm64" → client748/WYD Arm64 (+ touch_icons/).
# client748/project (x86_64) is untouched. Docker script may also copy project-aarch64.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
