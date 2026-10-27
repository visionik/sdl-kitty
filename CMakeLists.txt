cmake_minimum_required(VERSION 3.16)
project(sdl-kitty-meta C CXX)

option(SDL_KITTY_BUILD_EXAMPLES "Build sdl-kitty examples" ON)
option(SDL_KITTY_BUILD_IMGUI "Build Dear ImGui SDL3+SDL_Renderer example" ON)
option(SDL_KITTY_BUILD_NUKLEAR "Build Nuklear SDL3+SDL_Renderer demo" ON)
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
    target_compile_definitions(imgui_sdlrenderer3 PRIVATE ${_kitty_defs})

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
    target_compile_definitions(sdl_kitty_ask PRIVATE ${_kitty_defs})
  else()
    message(WARNING "SDL_KITTY_BUILD_IMGUI=ON but third_party/imgui missing; skip imgui_sdlrenderer3")
  endif()
endif()

# Nuklear official demo: SDL3 + SDL_Renderer (software-friendly for kitty).
# Upstream: third_party/Nuklear/demo/sdl3_renderer
if(SDL_KITTY_BUILD_NUKLEAR)
  set(_nuklear_dir "${CMAKE_SOURCE_DIR}/third_party/Nuklear")
  if(EXISTS "${_nuklear_dir}/nuklear.h")
    add_executable(nuklear_sdl3_renderer
      "${_nuklear_dir}/demo/sdl3_renderer/main.c"
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
    target_compile_definitions(nuklear_sdl3_renderer PRIVATE ${_kitty_defs})
    # Nuklear demo is written as C89; keep warnings usable on Clang.
    target_compile_options(nuklear_sdl3_renderer PRIVATE -Wno-unused-function)
  else()
    message(WARNING "SDL_KITTY_BUILD_NUKLEAR=ON but third_party/Nuklear missing; skip nuklear_sdl3_renderer")
  endif()
endif()

enable_testing()
add_subdirectory(tests)
