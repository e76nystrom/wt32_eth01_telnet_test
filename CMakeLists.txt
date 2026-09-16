cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# needs to be put before project()
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../gpsLib"
		"${CMAKE_CURRENT_SOURCE_DIR}/../u8g2")

message(STATUS "BUILD_TARGET = '${BUILD_TARGET}'")

set(VALID_TARGETS CLIENT SERVER)
if(NOT BUILD_TARGET IN_LIST VALID_TARGETS)
	message(FATAL_ERROR "BUILD_TARGET must be one of: ${VALID_TARGETS}")
endif()

#include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "-D${BUILD_TARGET}" APPEND)

idf_build_set_property(COMPILE_DEFINITIONS "-DDBG0_PIN=2" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DDBG1_PIN=4" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DPORT=8088" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-D${BUILD_TARGET}" APPEND)

#if(BUILD_TARGET STREQUAL "SERVER")
#    idf_build_set_property(COMPILE_DEFINITIONS "-DMY_PORT=8080" APPEND)
#else()
#    idf_build_set_property(COMPILE_DEFINITIONS "-DMY_PORT=9090" APPEND)
#endif()

project(wt32Eth01)

