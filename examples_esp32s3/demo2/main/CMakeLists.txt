


idf_component_register(
SRCS 
    "Font_Mono_Bold_30.c"
    "demo2.cpp"
    # "gif_play.cpp"
    "ft5536.cpp"
    "img_test.c"
PRIV_REQUIRES 
    spi_flash 
    FastEPD
    AnimatedGIF
    lvgl
INCLUDE_DIRS "")


# idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
#     target_compile_options(
#         ${LVGL_LIB} 
#         PRIVATE 
#             -DLV_LVGL_H_INCLUDE_SIMPLE
#     )

# if(CONFIG_LV_MEM_CUSTOM)
#     target_compile_options(
#         ${LVGL_LIB} 
#         PRIVATE 
#             "-DLV_MEM_CUSTOM_ALLOC(x)=heap_caps_malloc(x, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)"
#     )
# endif()
