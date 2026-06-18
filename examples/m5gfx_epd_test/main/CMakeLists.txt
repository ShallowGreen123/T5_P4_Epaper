idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        M5GFX
        driver
        esp_driver_i2c
        esp_timer
        t5_p4_board
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=deprecated-enum-enum-conversion>
)
