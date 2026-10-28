idf_component_register(
    SRCS
        "main.cpp"
        "t5_epub_board.cpp"
        "t5_epub_renderer.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_sdspi
        esp_driver_spi
        esp_timer
        fatfs
        sdmmc
        fastepd
        t5_p4_board
        epub_core
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-unused-variable
)
