idf_component_register(
    SRCS "main.cpp"
        "board_es8311.c"
    PRIV_REQUIRES esp_driver_i2s esp_driver_gpio
    EMBED_FILES "canon.pcm"
    INCLUDE_DIRS ""
)
