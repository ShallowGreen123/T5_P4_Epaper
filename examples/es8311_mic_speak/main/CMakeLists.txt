idf_component_register(
    SRCS "main.c"
        "board_es8311.c"
    PRIV_REQUIRES esp_driver_i2s esp_driver_i2c esp_driver_gpio t5_p4_board
    INCLUDE_DIRS ""
)
