idf_component_register(
    SRCS
        main.cpp
        epd_video.cpp
    INCLUDE_DIRS .
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_lcd
        esp_timer
)
