cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32p4")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(es8311_mic_speak)
