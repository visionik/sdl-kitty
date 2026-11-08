set(KITTY_RECT_SRC
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty/SDL_kittyrect.c")

add_executable(kitty_rect_utils_test
  kitty_rect_utils_test.c
  ${KITTY_RECT_SRC})
target_compile_definitions(kitty_rect_utils_test PRIVATE KITTY_RECT_STANDALONE=1)
target_include_directories(kitty_rect_utils_test PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty")
target_link_libraries(kitty_rect_utils_test PRIVATE SDL3::SDL3)

add_test(NAME kitty_rect_utils_test COMMAND kitty_rect_utils_test)

set(KITTY_PROBE_SRC
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty/SDL_kittyprobe.c")

add_executable(kitty_probe_utils_test
  kitty_probe_utils_test.c
  ${KITTY_PROBE_SRC})
target_compile_definitions(kitty_probe_utils_test PRIVATE KITTY_PROBE_STANDALONE=1)
target_include_directories(kitty_probe_utils_test PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty")
target_link_libraries(kitty_probe_utils_test PRIVATE SDL3::SDL3)

add_test(NAME kitty_probe_utils_test COMMAND kitty_probe_utils_test)

set(KITTY_PROFILE_SRC
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty/SDL_kittyprofile.c"
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty/SDL_kittyprofile_load.c"
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty/tomlc17/tomlc17.c")

add_executable(kitty_profile_utils_test
  kitty_profile_utils_test.c
  ${KITTY_PROFILE_SRC})
target_compile_definitions(kitty_profile_utils_test PRIVATE KITTY_PROFILE_STANDALONE=1)
target_include_directories(kitty_profile_utils_test PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty"
  "${CMAKE_SOURCE_DIR}/third_party/SDL/src/video/kitty/tomlc17")
target_link_libraries(kitty_profile_utils_test PRIVATE SDL3::SDL3)

add_test(NAME kitty_profile_utils_test COMMAND kitty_profile_utils_test)
# Resolve config/*.toml relative to the repo root regardless of cwd.
set_tests_properties(kitty_profile_utils_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# The compiled-in copy of config/*.toml, and the matrix in docs/terminals.md, must match them.
find_package(Python3 3.11 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME kitty_profile_data_fresh
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/update_facts.py" --check)
  add_test(NAME kitty_terminals_md_fresh
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/terminals_toml_to_md.py" --check)
endif()
