# SDL3 vendored helper for WYDLINUX.
# Usado quando o host não tem sdl3 pkg-config / find_package(SDL3).
# Assume SDL3 instalado em WYD_SDL3_ROOT (ou em third_party/sdl3-install).

if(NOT WYD_SDL3_ROOT AND DEFINED ENV{WYD_SDL3_ROOT})
  set(WYD_SDL3_ROOT "$ENV{WYD_SDL3_ROOT}")
endif()

if(NOT WYD_SDL3_ROOT)
  # Prefer a project-provided SDL3 install under build/sdl3/install.
  set(WYD_SDL3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../build/sdl3/install")
endif()

if(DEFINED ENV{WYD_SDL3_ROOT} AND NOT "$ENV{WYD_SDL3_ROOT}" STREQUAL "")
  set(WYD_SDL3_ROOT "$ENV{WYD_SDL3_ROOT}")
endif()  if(EXISTS "${WYD_SDL3_ROOT}/lib/cmake/SDL3/SDL3Config.cmake")
  set(SDL3_ROOT "${WYD_SDL3_ROOT}")
  include("${WYD_SDL3_ROOT}/lib/cmake/SDL3/SDL3Config.cmake")
  if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "SDL3Config.cmake presente mas sem SDL3::SDL3")
  endif()
  set(SDL3_INCLUDE_DIRS "${SDL3_ROOT}/include/SDL3")
  set(SDL3_LIBRARIES "SDL3::SDL3")
  set(SDL3_CFLAGS_OTHER "")
else()
  find_path(WYD_SDL3_INCLUDE_DIR
    NAMES SDL3/SDL.h
    PATHS "${WYD_SDL3_ROOT}/include"
    NO_DEFAULT_PATH
  )
  find_library(WYD_SDL3_LIBRARY
    NAMES SDL3 libSDL3.so
    PATHS "${WYD_SDL3_ROOT}/lib"
    NO_DEFAULT_PATH
  )
  if(NOT WYD_SDL3_INCLUDE_DIR OR NOT WYD_SDL3_LIBRARY)
    message(FATAL_ERROR
      "SDL3 vendado não encontrado em WYD_SDL3_ROOT=${WYD_SDL3_ROOT}\n"
      "Seta WYD_SDL3_ROOT ou constrói SDL3 em build/sdl3 (ver scripts/build-sdl3-in-docker.sh)"
    )
  endif()
  add_library(SDL3::SDL3 UNKNOWN IMPORTED)
  set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${WYD_SDL3_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WYD_SDL3_INCLUDE_DIR}"
  )
  set(SDL3_INCLUDE_DIRS "${WYD_SDL3_INCLUDE_DIR}")
  set(SDL3_LIBRARIES "SDL3::SDL3")
  set(SDL3_CFLAGS_OTHER "")
endif()
