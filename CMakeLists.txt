cmake_minimum_required(VERSION 3.10)
project(embedclaw C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform selection: POSIX (default) or FREERTOS
if(NOT DEFINED EC_PLATFORM)
    set(EC_PLATFORM "POSIX")
endif()

add_compile_definitions(EC_PLATFORM_${EC_PLATFORM})

if(EC_PLATFORM STREQUAL "POSIX")
    message(STATUS "Building for POSIX (host development)")
else()
    message(STATUS "Building for FreeRTOS (embedded target)")
    if(NOT DEFINED FREERTOS_PATH)
        message(FATAL_ERROR "FREERTOS_PATH must be set for FreeRTOS builds")
    endif()
endif()

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Library sources
set(EC_SOURCES
    src/ec_socket.c
    src/ec_http.c
    src/ec_json.c
    src/ec_api.c
    src/ec_tool.c
    src/ec_session.c
    src/ec_agent.c
    src/ec_io.c
    src/ec_io_uart.c
    src/ec_io_telnet.c
)

# Static library
add_library(embedclaw STATIC ${EC_SOURCES})
target_include_directories(embedclaw PUBLIC include)

# Demo executable
add_executable(embedclaw_demo src/main.c)
target_link_libraries(embedclaw_demo PRIVATE embedclaw)
