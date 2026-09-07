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
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    # These managed components still depend on the IDF 5.x driver aggregation
    # and identifiers. Keep the IDF 6 compatibility fixes project-local so
    # re-resolving managed components does not discard them.
    cmake_policy(SET CMP0079 NEW)
    target_link_libraries(${audio_player_lib} PUBLIC idf::esp_driver_i2s)

    idf_component_get_property(pca9535_lib wngfra__esp_io_expander_pca9535 COMPONENT_LIB)
    target_link_libraries(${pca9535_lib} PUBLIC idf::esp_driver_i2c)

    idf_component_get_property(t5_p4_board_lib t5_p4_board COMPONENT_LIB)
    target_compile_definitions(${t5_p4_board_lib} PRIVATE
        LCD_COLOR_PIXEL_FORMAT_RGB888=LCD_COLOR_FMT_RGB888)
endif()
target_compile_options(${audio_player_lib} PRIVATE "-Wno-error=ignored-qualifiers")
