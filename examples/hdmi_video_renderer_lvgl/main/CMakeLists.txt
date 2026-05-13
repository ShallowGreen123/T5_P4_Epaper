idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_timer
        espressif__esp_lcd_lt8912b
        t5_p4_board
    PRIV_REQUIRES
        esp_mm
)
