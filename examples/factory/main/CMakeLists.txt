file(GLOB FACTORY_ASSETS "asset/*.c")

idf_component_register(
    SRCS
        "main.cpp"
        "scr_mrg.cpp"
        "driver/factory_display.cpp"
        "driver/factory_touch.cpp"
        "port/factory_port.cpp"
        "ui/ui.cpp"
        "ui/ui_theme.cpp"
        "ui/screen_home.cpp"
        "ui/screen_display.cpp"
        "ui/screen_touch.cpp"
        "ui/screen_placeholder.cpp"
        "ui/screen_device.cpp"
        ${FACTORY_ASSETS}
    INCLUDE_DIRS
        "."
        "asset"
        "driver"
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
