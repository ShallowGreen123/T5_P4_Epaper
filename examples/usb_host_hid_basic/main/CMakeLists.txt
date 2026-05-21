idf_component_register(
    SRCS "usb_host_hid_basic_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES usb esp_driver_gpio
)
