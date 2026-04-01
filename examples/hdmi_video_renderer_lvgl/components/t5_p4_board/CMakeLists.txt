if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
    set(REQ esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_sdspi esp_driver_sdmmc fatfs)
    set(PRIV_REQ esp_driver_spi)
else()
    set(REQ driver fatfs)
    set(PRIV_REQ "")
endif()

idf_component_register(
    SRCS "t5_p4_board.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES ${REQ} esp_lcd
    PRIV_REQUIRES ${PRIV_REQ} esp_psram
)
