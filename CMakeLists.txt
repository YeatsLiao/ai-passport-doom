# AI-Passport Doom - Top-level CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# GBADoom source path (must be cloned separately)
set(GBADOOM_PATH "${CMAKE_CURRENT_LIST_DIR}/../GBADoom")

# Force sdkconfig.defaults to be applied
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ai_passport_doom)
