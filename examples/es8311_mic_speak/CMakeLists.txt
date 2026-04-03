# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# add_compile_options(
#     "-DBOARD_I2C_SDA=7" 
#     "-DBOARD_I2C_SCL=8"
#     "-DBOARD_I2C_NUM=I2C_NUM_0"
#     "-DBOARD_ES8311_I2S_MCLK=43"
#     "-DBOARD_ES8311_I2S_SCLK=42"
#     "-DBOARD_ES8311_I2S_ASDOUT=41"
#     "-DBOARD_ES8311_I2S_LRCK=40"
#     "-DBOARD_ES8311_I2S_DSDIN=39"
# )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(es8311)
