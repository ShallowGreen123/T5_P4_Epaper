file(GLOB LVGL_DEMO_ASSETS "src/*.c")

idf_component_register(
    SRCS
        "main.cpp"
        "ui.cpp"
        "ui_port.cpp"
        "scr_mrg.cpp"
        "idf_arduino_compat.cpp"
        ${LVGL_DEMO_ASSETS}
    INCLUDE_DIRS "." "src"
    REQUIRES
        driver
        esp_psram
        esp_timer
        fastepd
        lvgl
        sensorlib
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
