idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       REQUIRES bq25896 t5_p4_board)

if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_component_get_property(audio_player_lib chmorgan__esp-audio-player COMPONENT_LIB)
    target_link_libraries(${audio_player_lib} PUBLIC idf::esp_driver_i2s)
    idf_component_get_property(pca9535_lib wngfra__esp_io_expander_pca9535 COMPONENT_LIB)
    target_link_libraries(${pca9535_lib} PUBLIC idf::esp_driver_i2c)
    idf_component_get_property(board_lib t5_p4_board COMPONENT_LIB)
    target_compile_definitions(${board_lib} PRIVATE
        LCD_COLOR_PIXEL_FORMAT_RGB888=LCD_COLOR_FMT_RGB888)
endif()
