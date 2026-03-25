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
    src/ec_skill.c
    src/ec_skill_table.c
)

# Static library
add_library(embedclaw STATIC ${EC_SOURCES})
target_include_directories(embedclaw PUBLIC include)

# Demo executable
add_executable(embedclaw_demo src/main.c)
target_link_libraries(embedclaw_demo PRIVATE embedclaw)

# -------------------------------------------------------------------------
# End-to-end test executable
#
# Links the full stack EXCEPT ec_http.c (replaced by tests/mock_http.c) and
# ec_socket.c (not needed; mock_http.c provides ec_http_request directly).
# -------------------------------------------------------------------------
if(EC_PLATFORM STREQUAL "POSIX")
    set(EC_SOURCES_NO_HTTP
        src/ec_json.c
        src/ec_api.c
        src/ec_tool.c
        src/ec_session.c
        src/ec_agent.c
        src/ec_io.c
        src/ec_io_uart.c
        src/ec_io_telnet.c
        src/ec_skill.c
        src/ec_skill_table.c
    )

    add_executable(embedclaw_tests
        ${EC_SOURCES_NO_HTTP}
        tests/mock_http.c
        tests/test_e2e.c
    )
    target_include_directories(embedclaw_tests PRIVATE include tests)

    enable_testing()
    add_test(NAME e2e COMMAND embedclaw_tests)
endif()
