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
        esp_event
        esp_cam_sensor
        esp_driver_spi
        esp_hosted
        esp_netif
        esp_psram
        esp_hw_support
        esp_sccb_intf
        esp_timer
        esp_video
        esp_wifi
        fastepd
        bq25896
        bq27220
        lvgl
        nvs_flash
        sdmmc
        t5_p4_board
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
