cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32p4")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../fastEPD_lvgl_demo/components/fastepd"
    "${CMAKE_CURRENT_LIST_DIR}/../fastEPD_lvgl_demo/components/lvgl"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/bq25896"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/bq27220"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bq_power_dashboard)
