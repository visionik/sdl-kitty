cmake_minimum_required(VERSION 3.16)
project(sdl-kitty-meta C)

option(SDL_KITTY_BUILD_EXAMPLES "Build sdl-kitty examples" ON)
# Compile-time: present/debug stderr logs (SDL_KITTY_DEBUG env). Default OFF for perf.
option(SDL_KITTY_RUNTIME_DEBUG "Compile kitty runtime debug logging" OFF)
# Compile-time: wait for Gi=;OK after a=T (SDL_KITTY_PERF_ACK env). Default OFF.
option(SDL_KITTY_PERF_ACK "Compile optional PERF_ACK present waits" OFF)

# Build SDL3 with the Kitty video driver enabled (sources live in the submodule).
set(SDL_KITTY ON CACHE BOOL "Use Kitty graphics protocol video driver" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY ON CACHE BOOL "" FORCE)
set(SDL_TESTS ON CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/SDL)

# In-tree demos: fall back to repo config/terminals.toml when SDL_KITTY_PROFILES unset.
set(_kitty_defs "KITTY_DEFAULT_PROFILES_PATH=\"${CMAKE_SOURCE_DIR}/config/terminals.toml\"")
if(SDL_KITTY_RUNTIME_DEBUG)
  list(APPEND _kitty_defs "SDL_KITTY_RUNTIME_DEBUG=1")
endif()
if(SDL_KITTY_PERF_ACK)
  list(APPEND _kitty_defs "SDL_KITTY_PERF_ACK=1")
endif()
if(TARGET SDL3-shared)
  target_compile_definitions(SDL3-shared PRIVATE ${_kitty_defs})
endif()
if(TARGET SDL3-static)
  target_compile_definitions(SDL3-static PRIVATE ${_kitty_defs})
endif()

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
endif()

enable_testing()
add_subdirectory(tests)
