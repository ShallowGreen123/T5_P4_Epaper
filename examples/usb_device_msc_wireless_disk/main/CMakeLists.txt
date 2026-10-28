idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    EMBED_FILES "../spiffs/favicon.ico"
                "../spiffs/upload.html"
                "../spiffs/file_list_1.html"
                "../spiffs/file_list_2.html"
                "../spiffs/settings.html"
                "../spiffs/styles.css"
)

# esp_tinyusb 1.7.x still calls the pre-IDF-6 FATFS registration API.
# Redirect that call to the compatibility shim below while building on IDF 6.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_component_get_property(esp_tinyusb_lib espressif__esp_tinyusb COMPONENT_LIB)
    target_compile_options(${esp_tinyusb_lib} PRIVATE
        "-include${CMAKE_CURRENT_LIST_DIR}/esp_vfs_fat_legacy_compat.h")
endif()
