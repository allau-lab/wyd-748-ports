# Nintendo Switch — client real WYD 7.48 (SDL3 + D3D9→GLES)
# Build: ./scripts/build-switch-docker.sh
# Bring-up (quad 4 cores): -DWYD_SWITCH_BRINGUP=ON
# Client real (default):   -DWYD_SWITCH_CLIENT_LINK=ON
cmake_minimum_required(VERSION 3.16)
project(wyd748_switch LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  message(FATAL_ERROR "Use -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake (Docker)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT DEFINED DEVKITPRO)
  set(DEVKITPRO "$ENV{DEVKITPRO}")
endif()
set(SWITCH_PORTLIB "${DEVKITPRO}/portlibs/switch")
set(WYD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(TM_ROOT "${WYD_ROOT}/TMProject748")
set(WYD_OUT_DIR "${WYD_ROOT}/OUT/switch" CACHE PATH "Saída .nro/.elf")
# Semver do client Switch — incrementar a cada build publicado (NACP + HUD + diag).
set(WYD_SWITCH_VERSION "0.2.23")

option(WYD_SWITCH_BRINGUP "Build GLES bring-up smoke (nao e o jogo)" OFF)
option(WYD_SWITCH_CLIENT_LINK "Link client real TMProject748 + d3d9_gles" ON)

set(SWITCH_SDL3_PREFIX "" CACHE PATH "Prefix SDL3 switch-sdl-3.4")
if(SWITCH_SDL3_PREFIX)
  set(SDL3_SEARCH_PATH "${SWITCH_SDL3_PREFIX}")
else()
  set(SDL3_SEARCH_PATH "${SWITCH_PORTLIB}")
endif()

find_path(GLES_INCLUDE_DIR GLES3/gl3.h
  PATHS "${SWITCH_PORTLIB}/include" NO_DEFAULT_PATH)
find_library(GLESV2_LIBRARY NAMES GLESv2
  PATHS "${SWITCH_PORTLIB}/lib" NO_DEFAULT_PATH)
find_path(SWITCH_PORTLIB_INCLUDE_DIR curl/curl.h
  PATHS "${SWITCH_PORTLIB}/include" NO_DEFAULT_PATH)

if(NOT GLES_INCLUDE_DIR OR NOT GLESV2_LIBRARY)
  message(FATAL_ERROR "GLES portlib ausente no container")
endif()

# ---------------------------------------------------------------------------
# Bring-up (opcional) — SDL2 portlibs
# ---------------------------------------------------------------------------
if(WYD_SWITCH_BRINGUP)
  find_path(SDL2_INCLUDE_DIR SDL.h PATHS "${SWITCH_PORTLIB}/include/SDL2" NO_DEFAULT_PATH)
  find_library(SDL2_LIBRARY NAMES SDL2 PATHS "${SWITCH_PORTLIB}/lib" NO_DEFAULT_PATH)
  add_executable(wyd748_bringup
    "${CMAKE_CURRENT_SOURCE_DIR}/source/main_boot.cpp"
    "${WYD_ROOT}/platform/switch/wsi_switch.cpp"
    "${WYD_ROOT}/platform/switch/input_switch.cpp"
    "${WYD_ROOT}/platform/switch/d3d9_switch/d3d9_gles_bridge.cpp")
  target_include_directories(wyd748_bringup PRIVATE
    "${WYD_ROOT}/platform/switch" "${SDL2_INCLUDE_DIR}" "${GLES_INCLUDE_DIR}")
  target_compile_definitions(wyd748_bringup PRIVATE WYD_SWITCH=1)
  target_compile_options(wyd748_bringup PRIVATE -O2 -g -Wall -fsigned-char)
  target_link_libraries(wyd748_bringup PRIVATE
    "${SDL2_LIBRARY}" "${GLESV2_LIBRARY}" EGL glapi drm_nouveau nx)
  nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wyd748_bringup.nacp"
    NAME "WYD748 Bring-up" AUTHOR "WYDLINUX" VERSION "0.1.0")
  nx_create_nro(wyd748_bringup
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wyd748_bringup.nro"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/wyd748_bringup.nacp")
  add_custom_target(wyd748_bringup_pkg ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WYD_OUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_BINARY_DIR}/wyd748_bringup.nro" "${WYD_OUT_DIR}/wyd748_bringup.nro"
    DEPENDS wyd748_bringup_nro)
endif()

# ---------------------------------------------------------------------------
# Client real
# ---------------------------------------------------------------------------
if(WYD_SWITCH_CLIENT_LINK)
  find_path(SDL3_INCLUDE_DIR SDL3/SDL.h
    PATHS "${SDL3_SEARCH_PATH}/include" NO_DEFAULT_PATH)
  find_library(SDL3_LIBRARY NAMES SDL3
    PATHS "${SDL3_SEARCH_PATH}/lib" "${SDL3_SEARCH_PATH}/lib64" NO_DEFAULT_PATH)
  if(NOT SDL3_INCLUDE_DIR OR NOT SDL3_LIBRARY)
    message(FATAL_ERROR
      "SDL3 Switch nao encontrado em ${SDL3_SEARCH_PATH}. "
      "O script build-switch-docker.sh compila switch-sdl-3.4 no container.")
  endif()

  include("${WYD_ROOT}/cmake/tmproject_sources.cmake")
  include("${WYD_ROOT}/cmake/tmproject_includes.cmake")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "DirShow\\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "dsutil\\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "EventTranslator\\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "TMVideoWnd\\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "pch\\.cpp$")

  set(DXVK_INC "${WYD_ROOT}/third_party/dxvk-native-aarch64/usr/include/dxvk")
  if(NOT EXISTS "${DXVK_INC}/d3d9.h")
    set(DXVK_INC "${WYD_ROOT}/third_party/dxvk-native/usr/include/dxvk")
  endif()

  add_executable(wyd748
    ${TMPROJECT_SOURCES}
    "${WYD_ROOT}/platform/linux/compat/d3dx9_linux.cpp"
    "${WYD_ROOT}/platform/linux/compat/EventTranslator_linux.cpp"
    "${WYD_ROOT}/platform/linux/compat/TouchControlsUI.cpp"
    "${WYD_ROOT}/platform/switch/compat/switch_link_stubs.cpp"
    "${WYD_ROOT}/platform/linux/compat/dsutil_linux.cpp"
    "${WYD_ROOT}/platform/linux/compat/DirShow_linux.cpp"
    "${WYD_ROOT}/platform/linux/compat/wininet_linux.cpp"
    "${WYD_ROOT}/platform/linux/compat/wingdi_linux.cpp"
    "${WYD_ROOT}/platform/switch/compat/winuser_sdl.cpp"
    "${WYD_ROOT}/platform/linux/compat/TMVideoWnd_linux.cpp"
    "${WYD_ROOT}/platform/linux/compat/socket_poll_linux.cpp"
    "${WYD_ROOT}/platform/switch/d3d9_gles_switch.cpp"
    "${WYD_ROOT}/platform/switch/vs11_glsl.cpp"
    "${WYD_ROOT}/platform/switch/gl_hud9.cpp"
    "${WYD_ROOT}/platform/switch/switch_input.cpp"
    "${WYD_ROOT}/platform/switch/fullclient/client_main_switch.cpp"
  )

  target_include_directories(wyd748 PRIVATE
    # switch/compat PRIMEIRO: linux_pch.h / d3d9.h / iphlpapi.h / win32_extras.h
    "${WYD_ROOT}/platform/switch/compat"
    "${WYD_ROOT}/platform/switch"
    "${WYD_ROOT}/third_party/d3dx9_msvc"
    "${DXVK_INC}"
    "${WYD_ROOT}/platform/linux/compat"
    "${WYD_ROOT}/platform/linux"
    ${TMPROJECT_INCLUDE_DIRS}
    "${TM_ROOT}"
    "${WYD_ROOT}/third_party/minimp3"
    "${WYD_ROOT}/third_party"
    "${SDL3_INCLUDE_DIR}"
    "${GLES_INCLUDE_DIR}"
    "${SWITCH_PORTLIB_INCLUDE_DIR}"
  )

  target_compile_definitions(wyd748 PRIVATE
    WYD_SWITCH=1
    WYD_SDL3=1
    WYD_USE_SDL3=1
    WYD_LINUX=1
    WYD_GLES=1
    __SWITCH__=1
    _WYDCLIENT=1
    WYD_TEX_PROBE=1
    WYD_SWITCH_VERSION="${WYD_SWITCH_VERSION}"
  )

  target_compile_options(wyd748 PRIVATE
    -O2 -g -Wall -Wno-unused-parameter -Wno-unknown-pragmas
    -Wno-narrowing -fpermissive -fsigned-char -fexceptions -frtti
  )

  target_link_libraries(wyd748 PRIVATE
    "${SDL3_LIBRARY}"
    "${GLESV2_LIBRARY}"
    EGL
    glapi
    drm_nouveau
    curl
    mbedtls
    mbedx509
    mbedcrypto
    z
    nx
  )

  nx_generate_nacp(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wyd748.nacp"
    NAME "WYD 7.48"
    AUTHOR "WYDLINUX"
    VERSION "${WYD_SWITCH_VERSION}"
  )
  # Ícone do WYD.exe nativo (extraído → switch/icon/icon_256.jpg).
  set(WYD_SWITCH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon/icon_256.jpg")
  nx_create_nro(wyd748
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wyd748.nro"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/wyd748.nacp"
    ICON "${WYD_SWITCH_ICON}"
  )
  add_custom_target(wyd748_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WYD_OUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_BINARY_DIR}/wyd748.nro" "${WYD_OUT_DIR}/wyd748.nro"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:wyd748>" "${WYD_OUT_DIR}/wyd748.elf"
    DEPENDS wyd748_nro
    COMMENT "Instalando client real wyd748.nro"
  )

  message(STATUS "WYD748 Switch CLIENT real → ${WYD_OUT_DIR}/wyd748.nro")
endif()
