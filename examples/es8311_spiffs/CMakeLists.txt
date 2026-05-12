cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32p4")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options(-Wno-error=format)
project(es8311_spiffs)
