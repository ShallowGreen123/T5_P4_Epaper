# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 解决 esp32s3 串口没有输出的问题
add_compile_options("-DARDUINO_USB_CDC_ON_BOOT=1" 
                    "-DARDUINO_USB_MODE=1"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(demo1)
