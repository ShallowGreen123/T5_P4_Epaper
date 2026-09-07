# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
project(mipi_dsi_avoid_tearing)

# Suppress unused-variable warnings from managed component without touching its sources.
idf_component_get_property(ESP32_P4_EV_BOARD_LIB espressif__esp32_p4_function_ev_board COMPONENT_LIB)
if(ESP32_P4_EV_BOARD_LIB)
    target_compile_options(${ESP32_P4_EV_BOARD_LIB} PRIVATE -Wno-unused-variable)
endif()

# esp_lvgl_port 2.9.0 and esp_lcd_lt8912b 0.2.0 still use two IDF 5.5
# identifiers that changed in IDF 6.0. Keep the workaround target-local so the
# managed component sources remain untouched.
if(IDF_VERSION_MAJOR EQUAL 6 AND IDF_VERSION_MINOR EQUAL 0)
    if(TARGET lvgl_port_lib)
        target_compile_definitions(lvgl_port_lib PRIVATE
            on_frame_buf_complete=on_refresh_done)
    endif()

    if(ESP32_P4_EV_BOARD_LIB)
        target_compile_definitions(${ESP32_P4_EV_BOARD_LIB} PRIVATE
            LCD_COLOR_PIXEL_FORMAT_RGB888=LCD_COLOR_FMT_RGB888)
    endif()
endif()
