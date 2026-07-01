set(M5GFX_PANEL_EPD_COMPAT_SRC "")

if(IDF_TARGET STREQUAL "esp32p4")
    set(M5GFX_PANEL_EPD_COMPAT_SRC
        "${CMAKE_CURRENT_LIST_DIR}/../managed_components/m5stack__m5gfx/src/lgfx/v1/platforms/esp32/Panel_EPD.cpp"
    )
endif()

idf_component_register(
    SRCS
        "main.cpp"
        ${M5GFX_PANEL_EPD_COMPAT_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_driver_i2c
        esp_timer
        t5_p4_board
)

if(M5GFX_PANEL_EPD_COMPAT_SRC)
    set_source_files_properties(${M5GFX_PANEL_EPD_COMPAT_SRC} PROPERTIES
        COMPILE_DEFINITIONS "CONFIG_IDF_TARGET_ESP32S3=1"
    )
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=deprecated-enum-enum-conversion>
)
