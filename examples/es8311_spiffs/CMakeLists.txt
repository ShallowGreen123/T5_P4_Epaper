cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0079 NEW)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32p4")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-error=format)
project(es8311_spiffs)

# IDF 6 split the legacy driver aggregate into dedicated components. Keep
# pinned managed components linkable while they still include legacy headers.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_component_get_property(audio_player_lib chmorgan__esp-audio-player COMPONENT_LIB)
    if(audio_player_lib)
        target_link_libraries(${audio_player_lib} PUBLIC idf::esp_driver_i2s)
        target_include_directories(${audio_player_lib} PRIVATE
            "$ENV{IDF_PATH}/components/esp_driver_i2s/include")
        target_compile_options(${audio_player_lib} PRIVATE -Wno-error=ignored-qualifiers)
    endif()

    idf_component_get_property(pca9535_lib wngfra__esp_io_expander_pca9535 COMPONENT_LIB)
    if(pca9535_lib)
        target_link_libraries(${pca9535_lib} PUBLIC idf::esp_driver_i2c)
        target_include_directories(${pca9535_lib} PRIVATE
            "$ENV{IDF_PATH}/components/esp_driver_i2c/include")
    endif()

    idf_component_get_property(lt8912b_lib espressif__esp_lcd_lt8912b COMPONENT_LIB)
    if(lt8912b_lib)
        target_compile_definitions(${lt8912b_lib} PUBLIC
            LCD_COLOR_PIXEL_FORMAT_RGB888=LCD_COLOR_FMT_RGB888)
    endif()
endif()
