idf_component_register(
    SRCS
        "main.c"
        "board_es8311.c"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        cxx
        driver
        esp_driver_i2s
        spiffs
)

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
