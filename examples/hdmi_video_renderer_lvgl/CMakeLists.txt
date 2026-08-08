cmake_minimum_required(VERSION 3.5)
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

# Keep the legacy media-player components in the tree, but do not build them for
# this dedicated HDMI + LVGL demo runner.
set(COMPONENTS main t5_p4_board)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
project(hdmi_lvgl_demo_runner)

idf_component_get_property(audio_player_lib chmorgan__esp-audio-player COMPONENT_LIB)
target_compile_options(${audio_player_lib} PRIVATE "-Wno-error=ignored-qualifiers")
