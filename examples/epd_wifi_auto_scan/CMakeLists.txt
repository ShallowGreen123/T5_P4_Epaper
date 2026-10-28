cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32p4")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5gfx_wifi_auto_scan)

# IDF 6 split the legacy driver aggregate into dedicated components. The
# managed BSP dependencies still include legacy driver headers, so provide
# their public interfaces at the project boundary.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    cmake_policy(SET CMP0079 NEW)

    idf_component_get_property(audio_player_lib chmorgan__esp-audio-player COMPONENT_LIB)
    if(audio_player_lib)
        target_link_libraries(${audio_player_lib} PUBLIC idf::esp_driver_i2s)
        target_compile_options(${audio_player_lib} PRIVATE -Wno-error=ignored-qualifiers)
    endif()

    idf_component_get_property(pca9535_lib wngfra__esp_io_expander_pca9535 COMPONENT_LIB)
    if(pca9535_lib)
        target_link_libraries(${pca9535_lib} PUBLIC idf::esp_driver_i2c)
    endif()

    idf_component_get_property(lt8912b_lib espressif__esp_lcd_lt8912b COMPONENT_LIB)
    if(lt8912b_lib)
        target_compile_definitions(${lt8912b_lib} PUBLIC
            LCD_COLOR_PIXEL_FORMAT_RGB888=LCD_COLOR_FMT_RGB888)
    endif()

    idf_component_get_property(m5gfx_lib m5stack__m5gfx COMPONENT_LIB)
    if(m5gfx_lib)
        target_link_libraries(${m5gfx_lib} PUBLIC
            idf::esp_driver_i2s
            idf::esp_driver_ledc
        )
    endif()
endif()
