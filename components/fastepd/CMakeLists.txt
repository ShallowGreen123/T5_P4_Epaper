idf_component_register(
    SRCS "src/FastEPD.cpp"
         "src/Group5.cpp"
    INCLUDE_DIRS "src"
    REQUIRES driver esp_driver_i2c esp_timer esp_lcd
)
