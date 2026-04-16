idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        bq25896
        bq27220
        esp_driver_i2c
        esp_timer
        fastepd
        lvgl
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-missing-field-initializers
)
