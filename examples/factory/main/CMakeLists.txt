file(GLOB FACTORY_ASSETS "assets/*.c")
file(GLOB FACTORY_UI "ui/*.cpp")
file(GLOB FACTORY_PORT "port/*.cpp")

idf_component_register(
    SRCS
        "main.cpp"
        ${FACTORY_ASSETS}
        ${FACTORY_UI}
        ${FACTORY_PORT}
    INCLUDE_DIRS
        "."
        "assets"
        "port"
        "ui"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_lcd
        esp_psram
        esp_timer
        fastepd
        lvgl
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-unused-variable
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=deprecated-enum-enum-conversion>
)
