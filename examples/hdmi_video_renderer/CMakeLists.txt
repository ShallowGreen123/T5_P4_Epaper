# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0079 NEW)
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)
add_compile_options("-DLCD_COLOR_PIXEL_FORMAT_RGB888=LCD_COLOR_FMT_RGB888")
set(EXCLUDE_COMPONENTS fastepd)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes" "-Wno-error=ignored-qualifiers")
project(mipi_dsi_avoid_tearing)

# IDF 6.1 split the legacy driver headers into dedicated components.  The
# pinned third-party BSP dependencies still declare the old `driver` target,
# so provide their missing include/link interfaces at the project boundary.
if(TARGET __idf_chmorgan__esp-audio-player)
    target_link_libraries(__idf_chmorgan__esp-audio-player PUBLIC idf::esp_driver_i2s)
endif()
if(TARGET __idf_wngfra__esp_io_expander_pca9535)
    target_link_libraries(__idf_wngfra__esp_io_expander_pca9535 PUBLIC idf::esp_driver_i2c)
endif()
