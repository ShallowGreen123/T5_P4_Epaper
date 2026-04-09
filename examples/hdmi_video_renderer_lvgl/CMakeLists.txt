cmake_minimum_required(VERSION 3.5)

# Keep the legacy media-player components in the tree, but do not build them for
# this dedicated HDMI + LVGL demo runner.
set(COMPONENTS main t5_p4_board)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
project(hdmi_lvgl_demo_runner)
