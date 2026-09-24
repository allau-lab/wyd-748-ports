# SDL3 vendor glue for WYDLINUX.
# Prefer system sdl3 when available; otherwise use vendored install.
#
# Contract: SDL_INCLUDES must be a list of real include directories suitable for
# target_include_directories(... PRIVATE ${SDL_INCLUDES}). For SDL3 we do NOT
# append "/SDL3" here; SDL3 headers are included explicitly as <SDL3/SDL.h>
# in the compat layer when WYD_USE_SDL3 is enabled.

if(WYD_USE_SDL3)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    execute_process(
      COMMAND pkg-config --exists sdl3
      RESULT_VARIABLE _sdl3_pkg_result
      OUTPUT_QUIET
      ERROR_QUIET
    )
    if(_sdl3_pkg_result EQUAL 0)
      pkg_check_modules(SDL3 REQUIRED sdl3)
      message(STATUS "WYDLINUX: usando SDL3 do sistema (pkg-config sdl3)")
      set(_sdl_include_dirs "${SDL3_INCLUDE_DIRS}")
      set(_sdl_libraries "${SDL3_LIBRARIES}")
    else()
      message(STATUS "WYDLINUX: pkg-config encontrado, mas sdl3 não disponível; usando SDL3 vendado")
      set(_use_vendor_sdl3 TRUE)
    endif()
  else()
    message(STATUS "WYDLINUX: pkg-config não encontrado; usando SDL3 vendado")
    set(_use_vendor_sdl3 TRUE)
  endif()
  if(_use_vendor_sdl3)
    # Prefer a CMake-provided SDL3Config.cmake (system or project install).
    find_package(SDL3 QUIET)
    if(SDL3_FOUND AND TARGET SDL3::SDL3)
      message(STATUS "WYDLINUX: usando SDL3 via SDL3Config.cmake (SDL3_DIR=${SDL3_DIR})")
      set(_sdl_include_dirs "${SDL3_INCLUDE_DIRS}")
      set(_sdl_libraries "${SDL3_LIBRARIES}")
    else()
      include("${CMAKE_CURRENT_LIST_DIR}/sdl3-config.cmake")
      message(STATUS "WYDLINUX: usando SDL3 vendado em ${WYD_SDL3_ROOT}")
      set(_sdl_include_dirs "${SDL3_INCLUDE_DIRS}")
      set(_sdl_libraries "${SDL3_LIBRARIES}")
    endif()
  endif()
  # Keep the raw include dirs from pkg-config / SDL3Config. Do not append
  # "/SDL3" here: that produced bad/duplicated -I paths on the host build.
  set(SDL_INCLUDES "${_sdl_include_dirs}")
  set(SDL_LIBRARIES "${_sdl_libraries}")
  set(SDL_CFLAGS_OTHER "")
  set(SDL_PACKAGE "SDL3")
else()
  pkg_check_modules(SDL2 REQUIRED sdl2)
  set(SDL_INCLUDES "${SDL2_INCLUDE_DIRS}")
  set(SDL_LIBRARIES "${SDL2_LIBRARIES}")
  set(SDL_CFLAGS_OTHER "${SDL2_CFLAGS_OTHER}")
  set(SDL_PACKAGE "SDL2")
endif()
