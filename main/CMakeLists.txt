idf_component_register(SRCS "main.cpp" "test.cpp"
                       INCLUDE_DIRS "."
		       PRIV_REQUIRES gpsLib u8g2 oledLib esp_timer esp_driver_uart esp_driver_gpio
		       esp_driver_i2c esp_event esp_netif esp_eth nvs_flash)

set(CMAKE_CXX_STANDARD 17)

#message(STATUS "BUILD_TARGET = '${BUILD_TARGET}'")
#
#set(VALID_TARGETS CLIENT SERVER)
#if(NOT BUILD_TARGET IN_LIST VALID_TARGETS)
#	message(FATAL_ERROR "BUILD_TARGET must be one of: ${VALID_TARGETS}")
#endif()
#
#add_compile_definitions(${BUILD_TARGET})

