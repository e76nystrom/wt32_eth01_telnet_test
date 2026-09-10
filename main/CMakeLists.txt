idf_component_register(SRCS "main.c" "test.cpp"
                       INCLUDE_DIRS ".")

set(CMAKE_CXX_STANDARD 17)

# BUILD_TARGET must be "SERVER" or "CLIENT"
if(NOT DEFINED BUILD_TARGET)
    set(BUILD_TARGET "SERVER")  # default
endif()

if(BUILD_TARGET STREQUAL "SERVER")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SERVER)
elseif(BUILD_TARGET STREQUAL "CLIENT")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CLIENT)
else()
    message(FATAL_ERROR "BUILD_TARGET must be SERVER or CLIENT, got: ${BUILD_TARGET}")
endif()