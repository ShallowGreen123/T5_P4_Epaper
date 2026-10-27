cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0079 NEW)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32p4")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(factory)

# IDF 6.1 split the legacy driver headers into dedicated components.  The
# pinned third-party BSP dependencies still declare the old `driver` target,
# so provide their missing include/link interfaces at the project boundary.
if(TARGET __idf_chmorgan__esp-audio-player)
    target_link_libraries(__idf_chmorgan__esp-audio-player PUBLIC idf::esp_driver_i2s)
    target_compile_options(__idf_chmorgan__esp-audio-player PRIVATE -Wno-error=ignored-qualifiers)
endif()
if(TARGET __idf_espressif__esp_lcd_lt8912b)
    # Compatibility for the pixel format used by the pinned LT8912B headers.
    target_compile_definitions(__idf_espressif__esp_lcd_lt8912b PUBLIC
        LCD_COLOR_PIXEL_FORMAT_RGB888=LCD_COLOR_FMT_RGB888)
endif()
if(TARGET __idf_wngfra__esp_io_expander_pca9535)
    target_link_libraries(__idf_wngfra__esp_io_expander_pca9535 PUBLIC idf::esp_driver_i2c)
endif()
