cmake_minimum_required(VERSION 3.16)
project(sdl-kitty-meta C)

option(SDL_KITTY_BUILD_EXAMPLES "Build sdl-kitty examples" ON)

# Build SDL3 with the Kitty video driver enabled (sources live in the submodule).
set(SDL_KITTY ON CACHE BOOL "Use Kitty graphics protocol video driver" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/SDL)

if(SDL_KITTY_BUILD_EXAMPLES)
  add_executable(checker_keyboard examples/checker_keyboard.c)
  target_link_libraries(checker_keyboard PRIVATE SDL3::SDL3)

  add_executable(solid_present examples/solid_present.c)
  target_link_libraries(solid_present PRIVATE SDL3::SDL3)
endif()
