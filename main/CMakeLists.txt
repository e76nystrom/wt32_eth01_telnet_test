idf_component_register(SRCS "main.c" "test.cpp"
                       INCLUDE_DIRS "."
		       PRIV_REQUIRES gpsLib esp_timer esp_driver_uart esp_driver_gpio esp_event esp_netif esp_eth nvs_flash)

set(CMAKE_CXX_STANDARD 17)

message(STATUS "BUILD_TARGET = '${BUILD_TARGET}'")

set(VALID_TARGETS CLIENT SERVER)
if(NOT BUILD_TARGET IN_LIST VALID_TARGETS)
	message(FATAL_ERROR "BUILD_TARGET must be one of: ${VALID_TARGETS}")
endif()

add_compile_definitions(${BUILD_TARGET})

## BUILD_TARGET must be "SERVER" or "CLIENT"
#if(NOT DEFINED BUILD_TARGET)
#    set(BUILD_TARGET "SERVER")  # default
#endif()
#
#if(BUILD_TARGET STREQUAL "SERVER")
#    target_compile_definitions(${COMPONENT_LIB} PRIVATE SERVER)
#elseif(BUILD_TARGET STREQUAL "CLIENT")
#    target_compile_definitions(${COMPONENT_LIB} PRIVATE CLIENT)
#else()
#    message(FATAL_ERROR "BUILD_TARGET must be SERVER or CLIENT, got: ${BUILD_TARGET}")
#endif()
