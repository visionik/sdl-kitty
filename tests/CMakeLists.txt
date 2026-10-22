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