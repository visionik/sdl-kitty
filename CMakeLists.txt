cmake_minimum_required(VERSION 3.16)
project(sdl-kitty-meta C CXX)
if(APPLE)
  enable_language(OBJC)
endif()

option(SDL_KITTY_BUILD_EXAMPLES "Build sdl-kitty examples" ON)
option(SDL_KITTY_BUILD_IMGUI "Build Dear ImGui SDL3+SDL_Renderer example" ON)
option(SDL_KITTY_BUILD_NUKLEAR "Build Nuklear SDL3+SDL_Renderer demo" ON)
option(SDL_KITTY_BUILD_SDL2_COMPAT "Build bundled sdl2-compat so sdlk run supports SDL2 apps" ON)
option(SDL_KITTY_BUILD_SDL_TESTS "Build upstream SDL3 test suite" OFF)
option(SDL_KITTY_RELOCATABLE "Omit source-tree path for terminals.toml (for packaged installs)" OFF)
# Compile-time: present/debug stderr logs (SDL_KITTY_DEBUG env). Default OFF for perf.
option(SDL_KITTY_RUNTIME_DEBUG "Compile kitty runtime debug logging" OFF)
# Compile-time: wait for Gi=;OK after a=T (SDL_KITTY_PERF_ACK env). Default OFF.
option(SDL_KITTY_PERF_ACK "Compile optional PERF_ACK present waits" OFF)

include(GNUInstallDirs)

# Shared C99 Kitty pixel/protocol kernel (libkittyops), statically linked into libSDL3.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/libkittyops/CMakeLists.txt")
  set(KITTYOPS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(KITTYOPS_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(KITTYOPS_BUILD_TESTS ON CACHE BOOL "" FORCE)
  set(KITTYOPS_BUILD_BENCH OFF CACHE BOOL "" FORCE)
  set(KITTYOPS_PREFER_STATIC_DEPS ON CACHE BOOL "" FORCE)
  add_subdirectory(third_party/libkittyops)
endif()

# Build SDL3 with the Kitty video driver enabled (sources live in the submodule).
set(SDL_KITTY ON CACHE BOOL "Use Kitty graphics protocol video driver" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY ${SDL_KITTY_BUILD_SDL_TESTS} CACHE BOOL "" FORCE)
set(SDL_TESTS ${SDL_KITTY_BUILD_SDL_TESTS} CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/SDL)

# Optional bundled sdl2-compat: maps SDL2 calls directly onto our Kitty libSDL3
# so `sdlk run <sdl2-app>` runs existing SDL2 games/emulators in the terminal.
if(SDL_KITTY_BUILD_SDL2_COMPAT AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/sdl2-compat/CMakeLists.txt")
  set(SDL2COMPAT_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL2COMPAT_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL2COMPAT_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL2COMPAT_X11 OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/sdl2-compat)
endif()

# Both files are compiled into the driver (scripts/update_facts.py). In-tree builds
# also read the repo's copies at run time, so edits show without regenerating:
# config/terminals.toml (facts; SDL_KITTY_FACTS overrides) and
# config/sdl-kitty.toml (tuning; SDL_KITTY_PROFILES overrides).
set(_kitty_defs "")
if(NOT SDL_KITTY_RELOCATABLE)
  list(APPEND _kitty_defs
    "KITTY_DEFAULT_FACTS_PATH=\"${CMAKE_SOURCE_DIR}/config/terminals.toml\""
    "KITTY_DEFAULT_PROFILES_PATH=\"${CMAKE_SOURCE_DIR}/config/sdl-kitty.toml\""
  )
endif()
if(SDL_KITTY_RUNTIME_DEBUG)
  list(APPEND _kitty_defs "SDL_KITTY_RUNTIME_DEBUG=1")
endif()
if(SDL_KITTY_PERF_ACK)
  list(APPEND _kitty_defs "SDL_KITTY_PERF_ACK=1")
endif()
if(_kitty_defs)
  if(TARGET SDL3-shared)
    target_compile_definitions(SDL3-shared PRIVATE ${_kitty_defs})
  endif()
  if(TARGET SDL3-static)
    target_compile_definitions(SDL3-static PRIVATE ${_kitty_defs})
  endif()
endif()

set(_sdl_kitty_demo_targets "")

if(SDL_KITTY_BUILD_EXAMPLES)
  add_executable(checker_keyboard examples/checker_keyboard.c)
  target_link_libraries(checker_keyboard PRIVATE SDL3::SDL3)

  add_executable(key_probe examples/key_probe.c)
  target_link_libraries(key_probe PRIVATE SDL3::SDL3)

  add_executable(solid_present examples/solid_present.c)
  target_link_libraries(solid_present PRIVATE SDL3::SDL3)

  add_executable(static_present examples/static_present.c)
  target_link_libraries(static_present PRIVATE SDL3::SDL3)

  add_executable(mouse_paint examples/mouse_paint.c)
  target_link_libraries(mouse_paint PRIVATE SDL3::SDL3)

  add_executable(tui_card examples/tui_card.c)
  target_link_libraries(tui_card PRIVATE SDL3::SDL3)

  add_executable(dvd_bounce examples/dvd_bounce.c)
  target_link_libraries(dvd_bounce PRIVATE SDL3::SDL3)

  add_executable(dvd_sprite examples/dvd_sprite.c)
  target_link_libraries(dvd_sprite PRIVATE SDL3::SDL3)

  add_executable(atlas_sprites examples/atlas_sprites.c examples/kitty_apc.c)
  target_include_directories(atlas_sprites PRIVATE "${CMAKE_SOURCE_DIR}/examples")
  target_link_libraries(atlas_sprites PRIVATE SDL3::SDL3)

  add_executable(starfield examples/starfield.c)
  target_link_libraries(starfield PRIVATE SDL3::SDL3)

  add_executable(dirty_rects_demo examples/dirty_rects_demo.c)
  target_link_libraries(dirty_rects_demo PRIVATE SDL3::SDL3)

  add_executable(alpha_blit examples/alpha_blit.c)
  target_link_libraries(alpha_blit PRIVATE SDL3::SDL3)

  add_executable(fireworks examples/fireworks.c)
  target_link_libraries(fireworks PRIVATE SDL3::SDL3)

  add_executable(sdl_kitty_bench examples/sdl_kitty_bench.c)
  target_link_libraries(sdl_kitty_bench PRIVATE SDL3::SDL3)

  list(APPEND _sdl_kitty_demo_targets
    checker_keyboard
    key_probe
    solid_present
    static_present
    mouse_paint
    tui_card
    dvd_bounce
    dvd_sprite
    atlas_sprites
    starfield
    dirty_rects_demo
    alpha_blit
    fireworks
    sdl_kitty_bench
  )
endif()

# Dear ImGui official example: SDL3 + SDL_Renderer (software-friendly for kitty).
# Upstream: third_party/imgui/examples/example_sdl3_sdlrenderer3
# Launch with SDL_RENDER_DRIVER=software under the kitty video driver.
if(SDL_KITTY_BUILD_IMGUI)
  set(_imgui_dir "${CMAKE_SOURCE_DIR}/third_party/imgui")
  if(EXISTS "${_imgui_dir}/imgui.cpp")
    # Parent-owned main: Esc/Q quit for TUI card runs (upstream example has none).
    add_executable(imgui_sdlrenderer3
      "${CMAKE_SOURCE_DIR}/examples/imgui_sdlrenderer3_main.cpp"
      "${_imgui_dir}/imgui.cpp"
      "${_imgui_dir}/imgui_demo.cpp"
      "${_imgui_dir}/imgui_draw.cpp"
      "${_imgui_dir}/imgui_tables.cpp"
      "${_imgui_dir}/imgui_widgets.cpp"
      "${_imgui_dir}/backends/imgui_impl_sdl3.cpp"
      "${_imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp"
    )
    target_include_directories(imgui_sdlrenderer3 PRIVATE
      "${_imgui_dir}"
      "${_imgui_dir}/backends"
    )
    target_link_libraries(imgui_sdlrenderer3 PRIVATE SDL3::SDL3)
    # Match other demos: prefer in-tree terminals.toml when unset.
    if(_kitty_defs)
      target_compile_definitions(imgui_sdlrenderer3 PRIVATE ${_kitty_defs})
    endif()

    # CLI question card: radio / checkbox pick → result file (scripts/sdl-kitty-ask.sh).
    add_executable(sdl_kitty_ask
      "${CMAKE_SOURCE_DIR}/examples/sdl_kitty_ask.cpp"
      "${_imgui_dir}/imgui.cpp"
      "${_imgui_dir}/imgui_draw.cpp"
      "${_imgui_dir}/imgui_tables.cpp"
      "${_imgui_dir}/imgui_widgets.cpp"
      "${_imgui_dir}/backends/imgui_impl_sdl3.cpp"
      "${_imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp"
    )
    target_include_directories(sdl_kitty_ask PRIVATE
      "${_imgui_dir}"
      "${_imgui_dir}/backends"
    )
    target_link_libraries(sdl_kitty_ask PRIVATE SDL3::SDL3)
    if(_kitty_defs)
      target_compile_definitions(sdl_kitty_ask PRIVATE ${_kitty_defs})
    endif()
    list(APPEND _sdl_kitty_demo_targets imgui_sdlrenderer3 sdl_kitty_ask)
  else()
    message(WARNING "SDL_KITTY_BUILD_IMGUI=ON but third_party/imgui missing; skip imgui_sdlrenderer3")
  endif()
endif()

# Nuklear official demo: SDL3 + SDL_Renderer (software-friendly for kitty).
# Upstream: third_party/Nuklear/demo/sdl3_renderer
if(SDL_KITTY_BUILD_NUKLEAR)
  set(_nuklear_dir "${CMAKE_SOURCE_DIR}/third_party/Nuklear")
  if(EXISTS "${_nuklear_dir}/nuklear.h")
    # Parent-owned wrapper: adds Esc to quit and paces frames under software renderer.
    add_executable(nuklear_sdl3_renderer
      "${CMAKE_SOURCE_DIR}/examples/nuklear_sdl3_main.c"
    )
    target_include_directories(nuklear_sdl3_renderer PRIVATE
      "${_nuklear_dir}"
      "${_nuklear_dir}/demo/sdl3_renderer"
    )
    # SDL_MAIN_USE_CALLBACKS — need SDL3main for the entry stub on some platforms.
    if(TARGET SDL3::SDL3main)
      target_link_libraries(nuklear_sdl3_renderer PRIVATE SDL3::SDL3main SDL3::SDL3)
    else()
      target_link_libraries(nuklear_sdl3_renderer PRIVATE SDL3::SDL3)
    endif()
    if(UNIX AND NOT APPLE)
      target_link_libraries(nuklear_sdl3_renderer PRIVATE m)
    endif()
    if(_kitty_defs)
      target_compile_definitions(nuklear_sdl3_renderer PRIVATE ${_kitty_defs})
    endif()
    # Nuklear demo is written as C89; keep warnings usable on Clang.
    target_compile_options(nuklear_sdl3_renderer PRIVATE -Wno-unused-function)
    list(APPEND _sdl_kitty_demo_targets nuklear_sdl3_renderer)
  else()
    message(WARNING "SDL_KITTY_BUILD_NUKLEAR=ON but third_party/Nuklear missing; skip nuklear_sdl3_renderer")
  endif()
endif()

# ---------------------------------------------------------------------------
# Relocatable install layout (private lib/sdl-kitty avoids colliding with
# system or Homebrew sdl3 installs):
#   <prefix>/bin/sdlk
#   <prefix>/lib/sdl-kitty/libSDL3.{0.dylib,so.0}
#   <prefix>/share/sdl-kitty/terminals.toml
#   <prefix>/share/sdl-kitty/demos/<demo>
#   <prefix>/share/sdl-kitty/demos/assets/<png>
# ---------------------------------------------------------------------------
set(_sdl_kitty_priv_libdir "${CMAKE_INSTALL_LIBDIR}/sdl-kitty")
set(_sdl_kitty_datadir "${CMAKE_INSTALL_DATADIR}/sdl-kitty")
set(_sdl_kitty_demosdir "${_sdl_kitty_datadir}/demos")

if(APPLE)
  set(_sdl_kitty_demo_rpath "@loader_path/../../../${_sdl_kitty_priv_libdir}")
else()
  set(_sdl_kitty_demo_rpath "\$ORIGIN/../../../${_sdl_kitty_priv_libdir}")
endif()

foreach(_t IN LISTS _sdl_kitty_demo_targets)
  set_target_properties(${_t} PROPERTIES INSTALL_RPATH "${_sdl_kitty_demo_rpath}")
endforeach()

if(TARGET SDL3-shared)
  install(TARGETS SDL3-shared
    LIBRARY DESTINATION "${_sdl_kitty_priv_libdir}"
    RUNTIME DESTINATION "${_sdl_kitty_priv_libdir}"
  )
endif()
if(TARGET SDL2)
  install(TARGETS SDL2
    LIBRARY DESTINATION "${_sdl_kitty_priv_libdir}"
    RUNTIME DESTINATION "${_sdl_kitty_priv_libdir}"
  )
endif()

if(_sdl_kitty_demo_targets)
  install(TARGETS ${_sdl_kitty_demo_targets}
    RUNTIME DESTINATION "${_sdl_kitty_demosdir}"
  )
endif()

install(PROGRAMS "${CMAKE_SOURCE_DIR}/scripts/sdlk"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES
  "${CMAKE_SOURCE_DIR}/config/terminals.toml"
  "${CMAKE_SOURCE_DIR}/config/sdl-kitty.toml"
  DESTINATION "${_sdl_kitty_datadir}"
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples/assets"
  DESTINATION "${_sdl_kitty_demosdir}"
)

install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.txt"
  DESTINATION "${_sdl_kitty_datadir}/licenses"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL/LICENSE.txt")
  install(FILES "${CMAKE_SOURCE_DIR}/third_party/SDL/LICENSE.txt"
    DESTINATION "${_sdl_kitty_datadir}/licenses"
    RENAME "SDL-LICENSE.txt"
  )
endif()

enable_testing()
add_subdirectory(tests)
