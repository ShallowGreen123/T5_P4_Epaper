cmake_minimum_required(VERSION 3.16)

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/fastepd"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/lvgl"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fastEPD_lvgl_demo)
