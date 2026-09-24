cmake_minimum_required(VERSION 3.16)
project(WYDLINUX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WYD_BUILD_WAYLAND "Build Wayland bootstrap (SDL)" ON)
option(WYD_USE_SDL3 "Use SDL3 instead of system SDL2" OFF)
option(WYD_BUILD_NET_SMOKE "Build POSIX CPSock smoke" ON)
option(WYD_BUILD_ARCH_TESTS "Build ArchitectureTests" ON)
option(WYD_BUILD_SMOKE_PROJECT "Build demo project_main" OFF)
option(WYD_BUILD_REAL_CLIENT "Build TMProject748 nativo (DXVK + shims)" ON)
option(WYD_SDL3_COMPAT "Adicionar compat SDL3 no lado Linux (janela/input/áudio shims)" OFF)

set(WYD_DXVK_NATIVE_ROOT "" CACHE PATH "DXVK Native prefix (usr/include + usr/lib)")

set(WYD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(TM_ROOT "${WYD_ROOT}/TMProject748")
set(LINUX_PLATFORM "${WYD_ROOT}/platform/linux")

add_compile_definitions(WYD_LINUX=1)

# Resolve DXVK once for smoke targets and wyd_client. Cross builds must not
# silently reuse the host x86_64 .so tree.
if(WYD_DXVK_NATIVE_ROOT)
  set(DXVK_NATIVE_ROOT "${WYD_DXVK_NATIVE_ROOT}")
elseif(DEFINED ENV{WYD_DXVK_NATIVE_ROOT} AND NOT "$ENV{WYD_DXVK_NATIVE_ROOT}" STREQUAL "")
  set(DXVK_NATIVE_ROOT "$ENV{WYD_DXVK_NATIVE_ROOT}")
else()
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _wyd_proc)
  if(_wyd_proc MATCHES "aarch64|arm64")
    set(DXVK_NATIVE_ROOT "${WYD_ROOT}/third_party/dxvk-native-aarch64")
  else()
    set(DXVK_NATIVE_ROOT "${WYD_ROOT}/third_party/dxvk-native")
  endif()
endif()
set(WYD_HAS_DXVK_NATIVE_HEADERS 0)
if(EXISTS "${DXVK_NATIVE_ROOT}/usr/include/dxvk/d3d9.h")
  set(WYD_HAS_DXVK_NATIVE_HEADERS 1)
endif()
message(STATUS "WYDLINUX: arch=${CMAKE_SYSTEM_PROCESSOR} cross=${CMAKE_CROSSCOMPILING} dxvk=${DXVK_NATIVE_ROOT} headers=${WYD_HAS_DXVK_NATIVE_HEADERS}")

# ---------------------------------------------------------------------------
# ArchitectureTests (wire/ABI — sem Win32/DX/Wayland)
# ---------------------------------------------------------------------------
if(WYD_BUILD_ARCH_TESTS)
  add_executable(ArchitectureTests
    "${TM_ROOT}/tests/PacketViewTests.cpp"
    "${TM_ROOT}/tests/CharacterLoginUseCaseTests.cpp"
    "${TM_ROOT}/tests/ReceivedPacketDispatchTests.cpp"
    "${TM_ROOT}/tests/CargoSlotTests.cpp"
    "${TM_ROOT}/tests/GridInsertionTests.cpp"
  )
  target_include_directories(ArchitectureTests PRIVATE
    "${TM_ROOT}"
    "${TM_ROOT}/CommonFiles"
    "${TM_ROOT}/internal"
    "${TM_ROOT}/internal/application"
    "${TM_ROOT}/internal/application/ports"
    "${TM_ROOT}/internal/wire"
    "${TM_ROOT}/internal/platform/windows"
    "${TM_ROOT}/internal/platform/network"
    "${LINUX_PLATFORM}"
  )
  target_compile_options(ArchitectureTests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# net_smoke — CPSock POSIX
# ---------------------------------------------------------------------------
if(WYD_BUILD_NET_SMOKE)
  add_executable(net_smoke
    "${WYD_ROOT}/cmd/net_smoke.cpp"
    "${TM_ROOT}/internal/platform/windows/CPSock.cpp"
    "${LINUX_PLATFORM}/compat/socket_poll_linux.cpp"
  )
  target_include_directories(net_smoke PRIVATE
    "${LINUX_PLATFORM}"
    "${LINUX_PLATFORM}/compat"
    "${DXVK_NATIVE_ROOT}/usr/include/dxvk"
    "${TM_ROOT}"
    "${TM_ROOT}/CommonFiles"
    "${TM_ROOT}/internal"
    "${TM_ROOT}/internal/application/ports"
    "${TM_ROOT}/internal/wire"
    "${TM_ROOT}/internal/platform/windows"
    "${TM_ROOT}/internal/platform/network"
  )
  target_compile_options(net_smoke PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# wyd-wayland — janela Wayland + Clear/Present Vulkan (ponte D3D9)
# ---------------------------------------------------------------------------
if(WYD_BUILD_WAYLAND)
  find_package(PkgConfig REQUIRED)
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/sdl3-vendor.cmake")
  pkg_check_modules(WAYLAND REQUIRED wayland-client)
  find_library(VULKAN_LIB vulkan REQUIRED)

  add_executable(wyd-wayland
    "${WYD_ROOT}/cmd/wyd_wayland_main.cpp"
    "${LINUX_PLATFORM}/wayland_window.cpp"
    "${LINUX_PLATFORM}/vulkan_present.cpp"
    "${LINUX_PLATFORM}/d3d9_bridge.cpp"
  )
  target_include_directories(wyd-wayland PRIVATE
    "${LINUX_PLATFORM}"
    ${SDL_INCLUDES}
    ${WAYLAND_INCLUDE_DIRS}
  )
  target_link_libraries(wyd-wayland PRIVATE
    ${SDL_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${VULKAN_LIB}
  )
  target_compile_options(wyd-wayland PRIVATE -Wall -Wextra ${SDL_CFLAGS_OTHER})
  target_compile_definitions(wyd-wayland PRIVATE
    WYD_WAYLAND=1
    WYD_VULKAN=1
    $<$<BOOL:${WYD_USE_SDL3}>:WYD_USE_SDL3=1>
  )

  add_executable(d3d9_min_smoke
    "${WYD_ROOT}/cmd/d3d9_min_smoke.cpp"
    "${LINUX_PLATFORM}/wayland_window.cpp"
    "${LINUX_PLATFORM}/vulkan_present.cpp"
    "${LINUX_PLATFORM}/d3d9_bridge.cpp"
  )
  target_include_directories(d3d9_min_smoke PRIVATE
    "${LINUX_PLATFORM}"
    ${SDL_INCLUDES}
    ${WAYLAND_INCLUDE_DIRS}
  )
  target_link_libraries(d3d9_min_smoke PRIVATE
    ${SDL_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${VULKAN_LIB}
  )
  target_compile_options(d3d9_min_smoke PRIVATE -Wall -Wextra ${SDL_CFLAGS_OTHER})
  target_compile_definitions(d3d9_min_smoke PRIVATE
    WYD_WAYLAND=1
    WYD_VULKAN=1
    $<$<BOOL:${WYD_USE_SDL3}>:WYD_USE_SDL3=1>
  )

  add_executable(asset_render_smoke
    "${WYD_ROOT}/cmd/asset_render_smoke.cpp"
    "${LINUX_PLATFORM}/wayland_window.cpp"
    "${LINUX_PLATFORM}/vulkan_present.cpp"
    "${LINUX_PLATFORM}/vulkan_textured.cpp"
    "${LINUX_PLATFORM}/wyt_decode.cpp"
    "${LINUX_PLATFORM}/shader_dx9_catalog.cpp"
  )
  target_include_directories(asset_render_smoke PRIVATE
    "${LINUX_PLATFORM}"
    ${SDL_INCLUDES}
    ${WAYLAND_INCLUDE_DIRS}
  )
  target_link_libraries(asset_render_smoke PRIVATE
    ${SDL_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${VULKAN_LIB}
  )
  target_compile_options(asset_render_smoke PRIVATE -Wall -Wextra ${SDL_CFLAGS_OTHER})
  target_compile_definitions(asset_render_smoke PRIVATE
    WYD_WAYLAND=1
    WYD_VULKAN=1
    $<$<BOOL:${WYD_USE_SDL3}>:WYD_USE_SDL3=1>
  )

  add_executable(d3pp_smoke
    "${WYD_ROOT}/cmd/d3pp_smoke.cpp"
    "${LINUX_PLATFORM}/wayland_window.cpp"
    "${LINUX_PLATFORM}/vulkan_present.cpp"
    "${LINUX_PLATFORM}/vulkan_mesh.cpp"
    "${LINUX_PLATFORM}/msa_loader.cpp"
    "${LINUX_PLATFORM}/object_mask.cpp"
    "${LINUX_PLATFORM}/wyt_decode.cpp"
    "${LINUX_PLATFORM}/shader_dx9_catalog.cpp"
  )
  target_include_directories(d3pp_smoke PRIVATE
    "${LINUX_PLATFORM}"
    ${SDL_INCLUDES}
    ${WAYLAND_INCLUDE_DIRS}
  )
  target_link_libraries(d3pp_smoke PRIVATE
    ${SDL_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${VULKAN_LIB}
  )
  target_compile_options(d3pp_smoke PRIVATE -Wall -Wextra ${SDL_CFLAGS_OTHER})
  target_compile_definitions(d3pp_smoke PRIVATE
    WYD_WAYLAND=1
    WYD_VULKAN=1
    $<$<BOOL:${WYD_USE_SDL3}>:WYD_USE_SDL3=1>
  )

  # DXVK_NATIVE_ROOT / WYD_HAS_DXVK_NATIVE_HEADERS resolved at top of file.
  add_executable(dxvk_native_smoke
    "${WYD_ROOT}/cmd/dxvk_native_smoke.cpp"
    "${LINUX_PLATFORM}/wayland_window.cpp"
    "${LINUX_PLATFORM}/d3d9_dxvk_native.cpp"
    "${LINUX_PLATFORM}/shader_dx9_catalog.cpp"
  )
  target_include_directories(dxvk_native_smoke PRIVATE
    "${LINUX_PLATFORM}"
    ${SDL_INCLUDES}
    ${WAYLAND_INCLUDE_DIRS}
  )
  if(WYD_HAS_DXVK_NATIVE_HEADERS)
    target_include_directories(dxvk_native_smoke PRIVATE
      "${DXVK_NATIVE_ROOT}/usr/include/dxvk")
  endif()
  target_link_libraries(dxvk_native_smoke PRIVATE
    ${SDL_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${VULKAN_LIB}
    dl
  )
  target_compile_options(dxvk_native_smoke PRIVATE
    -Wall -Wextra -Wno-unused-parameter ${SDL_CFLAGS_OTHER})
  target_compile_definitions(dxvk_native_smoke PRIVATE
    WYD_WAYLAND=1
    WYD_VULKAN=1
    WYD_HAS_DXVK_NATIVE_HEADERS=${WYD_HAS_DXVK_NATIVE_HEADERS}
    $<$<BOOL:${WYD_USE_SDL3}>:WYD_USE_SDL3=1>
  )

  # Demo legado (project_main) — OFF por padrão; client real = wyd_client
  if(WYD_BUILD_SMOKE_PROJECT)
    add_executable(wyd_project
      "${WYD_ROOT}/cmd/project_main.cpp"
      "${LINUX_PLATFORM}/wayland_window.cpp"
      "${LINUX_PLATFORM}/vulkan_present.cpp"
      "${LINUX_PLATFORM}/vulkan_textured.cpp"
      "${LINUX_PLATFORM}/vulkan_mesh.cpp"
      "${LINUX_PLATFORM}/wyt_decode.cpp"
      "${LINUX_PLATFORM}/wys_decode.cpp"
      "${LINUX_PLATFORM}/shader_dx9_catalog.cpp"
      "${LINUX_PLATFORM}/dx9_shader_runtime.cpp"
      "${LINUX_PLATFORM}/d3d9_dxvk_native.cpp"
      "${LINUX_PLATFORM}/object_mask.cpp"
      "${LINUX_PLATFORM}/sn_loader.cpp"
      "${LINUX_PLATFORM}/audio_sdl.cpp"
      "${LINUX_PLATFORM}/login_session.cpp"
      "${LINUX_PLATFORM}/selchar_panel.cpp"
      "${LINUX_PLATFORM}/field_map.cpp"
      "${LINUX_PLATFORM}/field_view.cpp"
      "${LINUX_PLATFORM}/field_entities.cpp"
      "${LINUX_PLATFORM}/msa_loader.cpp"
      "${LINUX_PLATFORM}/msh_loader.cpp"
      "${LINUX_PLATFORM}/bone_skin.cpp"
      "${LINUX_PLATFORM}/bone_ani_catalog.cpp"
      "${TM_ROOT}/internal/platform/windows/CPSock.cpp"
    )
    target_include_directories(wyd_project PRIVATE
      "${LINUX_PLATFORM}"
      "${TM_ROOT}/internal/wire"
      "${TM_ROOT}/internal/application/ports"
      "${TM_ROOT}/internal/platform/windows"
      "${TM_ROOT}/internal/platform/network"
      ${SDL_INCLUDES}
      ${WAYLAND_INCLUDE_DIRS}
    )
    if(WYD_HAS_DXVK_NATIVE_HEADERS)
      target_include_directories(wyd_project PRIVATE
        "${DXVK_NATIVE_ROOT}/usr/include/dxvk")
    endif()
    target_link_libraries(wyd_project PRIVATE
      ${SDL_LIBRARIES}
      ${WAYLAND_LIBRARIES}
      ${VULKAN_LIB}
      dl
    )
    target_compile_options(wyd_project PRIVATE
      -Wall -Wextra -Wno-unused-parameter ${SDL_CFLAGS_OTHER})
    target_compile_definitions(wyd_project PRIVATE
      WYD_WAYLAND=1
      WYD_VULKAN=1
      WYD_LINUX=1
      WYD_HAS_DXVK_NATIVE_HEADERS=${WYD_HAS_DXVK_NATIVE_HEADERS}
      $<$<BOOL:${WYD_USE_SDL3}>:WYD_USE_SDL3=1>
    )
  endif() # WYD_BUILD_SMOKE_PROJECT

  set(WYD_CLIENT748_DIR "${WYD_ROOT}/client748")
  if(EXISTS "${WYD_CLIENT748_DIR}")
    if(WYD_BUILD_SMOKE_PROJECT)
      add_custom_command(TARGET wyd_project POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:wyd_project> "${WYD_CLIENT748_DIR}/project"
        COMMENT "Instalando candidato Linux em client748/project"
        VERBATIM
      )
    endif()
    add_custom_command(TARGET d3pp_smoke POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:d3pp_smoke> "${WYD_CLIENT748_DIR}/d3pp_smoke"
      VERBATIM
    )
    add_custom_command(TARGET wyd-wayland POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:wyd-wayland> "${WYD_CLIENT748_DIR}/wyd-wayland"
      COMMENT "Instalando wyd-wayland em client748/"
      VERBATIM
    )
    add_custom_command(TARGET asset_render_smoke POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:asset_render_smoke> "${WYD_CLIENT748_DIR}/asset_render_smoke"
      VERBATIM
    )
    add_custom_command(TARGET d3d9_min_smoke POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:d3d9_min_smoke> "${WYD_CLIENT748_DIR}/d3d9_min_smoke"
      VERBATIM
    )
    add_custom_command(TARGET dxvk_native_smoke POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dxvk_native_smoke> "${WYD_CLIENT748_DIR}/dxvk_native_smoke"
      VERBATIM
    )
    message(STATUS "WYDLINUX: POST_BUILD → ${WYD_CLIENT748_DIR}/ (smokes)")
  else()
    message(WARNING "WYDLINUX: client748/ ausente — build não instalará o binário de teste")
  endif()
endif()

if(WYD_BUILD_NET_SMOKE AND EXISTS "${WYD_ROOT}/client748")
  add_custom_command(TARGET net_smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:net_smoke> "${WYD_ROOT}/client748/net_smoke"
    VERBATIM
  )
endif()

if(WYD_BUILD_ARCH_TESTS AND EXISTS "${WYD_ROOT}/client748")
  add_custom_command(TARGET ArchitectureTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ArchitectureTests> "${WYD_ROOT}/client748/ArchitectureTests"
    VERBATIM
  )
endif()

message(STATUS "WYDLINUX: client real = TMProject748 + DXVK (ver PORT.md)")

# ---------------------------------------------------------------------------
# Client REAL (TMProject748) — alvo principal do port
# ---------------------------------------------------------------------------
if(WYD_BUILD_REAL_CLIENT AND WYD_HAS_DXVK_NATIVE_HEADERS)
  include("${WYD_ROOT}/cmake/tmproject_sources.cmake")
  include("${WYD_ROOT}/cmake/tmproject_includes.cmake")

  # Mídia Win32 → backends Linux reais (SDL áudio/input, curl, minimp3).
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "DirShow\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "dsutil\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "pch\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "EventTranslator\.cpp$")
  list(FILTER TMPROJECT_SOURCES EXCLUDE REGEX "TMVideoWnd\.cpp$")

  find_package(CURL REQUIRED)
  set(DXVK_D3D9_LIB "${DXVK_NATIVE_ROOT}/usr/lib/libdxvk_d3d9.so")

  add_executable(wyd_client
    ${TMPROJECT_SOURCES}
    "${WYD_ROOT}/cmd/linux_client_main.cpp"
    "${LINUX_PLATFORM}/compat/d3dx9_linux.cpp"
    "${LINUX_PLATFORM}/compat/EventTranslator_linux.cpp"
    "${LINUX_PLATFORM}/compat/TouchControlsUI.cpp"
    "${LINUX_PLATFORM}/compat/dsutil_linux.cpp"
    "${LINUX_PLATFORM}/compat/DirShow_linux.cpp"
    "${LINUX_PLATFORM}/compat/wininet_linux.cpp"
    "${LINUX_PLATFORM}/compat/wingdi_linux.cpp"
    "${LINUX_PLATFORM}/compat/winuser_sdl.cpp"
    "${LINUX_PLATFORM}/compat/TMVideoWnd_linux.cpp"
    "${LINUX_PLATFORM}/compat/socket_poll_linux.cpp"
  )
  target_include_directories(wyd_client PRIVATE
    "${LINUX_PLATFORM}/compat"
    ${TMPROJECT_INCLUDE_DIRS}
    "${TM_ROOT}"
    "${LINUX_PLATFORM}"
    "${DXVK_NATIVE_ROOT}/usr/include/dxvk"
    "${WYD_ROOT}/third_party/minimp3"
    "${WYD_ROOT}/third_party"
    ${SDL_INCLUDES}
    ${CURL_INCLUDE_DIRS}
  )
  target_compile_definitions(wyd_client PRIVATE
    WYD_LINUX=1
    WYD_WAYLAND=1
    WYD_HAS_DXVK_NATIVE_HEADERS=1
    $<$<BOOL:${WYD_USE_SDL3}>:WYD_USE_SDL3=1>
  )
  target_compile_options(wyd_client PRIVATE
    -Wall -Wno-unused-parameter -Wno-unknown-pragmas -fpermissive -Wno-narrowing
    ${SDL_CFLAGS_OTHER}
  )
  target_link_directories(wyd_client PRIVATE "${DXVK_NATIVE_ROOT}/usr/lib")
  target_link_libraries(wyd_client PRIVATE
    ${SDL_LIBRARIES}
    ${CURL_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${VULKAN_LIB}
    "${DXVK_D3D9_LIB}"
    dl
    pthread
  )
  target_link_options(wyd_client PRIVATE -rdynamic)
  set_target_properties(wyd_client PROPERTIES
    BUILD_RPATH "${DXVK_NATIVE_ROOT}/usr/lib"
    INSTALL_RPATH "${DXVK_NATIVE_ROOT}/usr/lib"
  )
  # Never overwrite the playable x86_64 client748/project when cross-compiling.
  if(EXISTS "${WYD_ROOT}/client748" AND NOT CMAKE_CROSSCOMPILING)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _wyd_host_proc)
    if(NOT _wyd_host_proc MATCHES "aarch64|arm64")
      add_custom_command(TARGET wyd_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:wyd_client> "${WYD_ROOT}/client748/project"
        COMMENT "Instalando CLIENT REAL em client748/project"
        VERBATIM
      )
    else()
      set_target_properties(wyd_client PROPERTIES OUTPUT_NAME "WYD Arm64")
      add_custom_command(TARGET wyd_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:wyd_client> "${WYD_ROOT}/client748/WYD Arm64"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${WYD_ROOT}/client748/touch_icons"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets/touch_icons" "${WYD_ROOT}/client748/touch_icons"
        COMMENT "Instalando CLIENT ARM64 + touch_icons em client748/"
        VERBATIM
      )
    endif()
  elseif(EXISTS "${WYD_ROOT}/client748" AND CMAKE_CROSSCOMPILING)
    set_target_properties(wyd_client PROPERTIES OUTPUT_NAME "WYD Arm64")
    add_custom_command(TARGET wyd_client POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:wyd_client> "${WYD_ROOT}/client748/WYD Arm64"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${WYD_ROOT}/client748/touch_icons"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/touch_icons" "${WYD_ROOT}/client748/touch_icons"
      COMMENT "Instalando cross-build + touch_icons em client748/"
      VERBATIM
    )
  endif()
  message(STATUS "WYDLINUX: wyd_client (TMProject real) habilitado")
  if(WYD_SDL3_COMPAT)
    message(STATUS "WYDLINUX: WYD_SDL3_COMPAT=ON — inclui compat SDL3 (ver SDL3/)")
  endif()
elseif(WYD_BUILD_REAL_CLIENT)
  message(WARNING "WYDLINUX: wyd_client pediu DXVK Native headers — rode scripts/fetch-dxvk-native.sh (ou o variante aarch64)")
endif()

# ---------------------------------------------------------------------------
# Compat SDL3 (port lateral, não afeta client real ainda)
# ---------------------------------------------------------------------------
if(WYD_BUILD_SDL3_COMPAT)
  message(STATUS "WYDLINUX: WYD_BUILD_SDL3_COMPAT=ON — build de compat SDL3")
  # placeholder: aqui entraria cmake SDL3 + arquivos de compat SDL3
endif()
