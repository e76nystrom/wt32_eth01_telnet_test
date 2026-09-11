cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# needs to be put before project()
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../gpsLib")

message(STATUS "BUILD_TARGET = '${BUILD_TARGET}'")

project(wt32Eth01)

