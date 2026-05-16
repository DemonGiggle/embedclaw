cmake_minimum_required(VERSION 3.10)
project(embedclaw C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform selection: POSIX (default), FREERTOS, or BAREMETAL
if(NOT DEFINED EC_PLATFORM)
    set(EC_PLATFORM "POSIX")
endif()

add_compile_definitions(EC_PLATFORM_${EC_PLATFORM})

option(EC_HOST_SIM "Enable explicit host simulation profile (POSIX only)" OFF)

if(EC_PLATFORM STREQUAL "POSIX")
    if(EC_HOST_SIM)
        message(STATUS "Building for POSIX host simulation")
    else()
        message(STATUS "Building for POSIX (host development)")
    endif()
elseif(EC_PLATFORM STREQUAL "FREERTOS")
    if(EC_HOST_SIM)
        message(FATAL_ERROR "EC_HOST_SIM is only supported with EC_PLATFORM=POSIX")
    endif()
    message(STATUS "Building for FreeRTOS (embedded target)")
    if(NOT DEFINED FREERTOS_PATH)
        message(FATAL_ERROR "FREERTOS_PATH must be set for FreeRTOS builds")
    endif()
elseif(EC_PLATFORM STREQUAL "BAREMETAL")
    if(EC_HOST_SIM)
        message(FATAL_ERROR "EC_HOST_SIM is only supported with EC_PLATFORM=POSIX")
    endif()
    message(STATUS "Building for bare-metal embedded target")
else()
    message(FATAL_ERROR "Unsupported EC_PLATFORM='${EC_PLATFORM}'")
endif()

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# -------------------------------------------------------------------------
# TLS support via mbedTLS (optional, ON by default)
# -------------------------------------------------------------------------
option(EC_ENABLE_TLS "Enable TLS/HTTPS support via mbedTLS" ON)

if(EC_ENABLE_TLS AND NOT EC_PLATFORM STREQUAL "BAREMETAL")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/mbedtls/CMakeLists.txt")
        message(FATAL_ERROR
            "mbedTLS submodule not found at third_party/mbedtls.\n"
            "Run:  git submodule update --init\n"
            "Or disable TLS:  cmake -DEC_ENABLE_TLS=OFF ..")
    endif()

    # Suppress mbedTLS tests and sample programs
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)

    add_subdirectory(third_party/mbedtls EXCLUDE_FROM_ALL)
endif()

# Shared runtime sources. These files are platform-neutral and must not include
# POSIX, FreeRTOS, or board-vendor headers.
set(EC_CORE_SOURCES
    src/core/ec_hw_access.c
    src/core/ec_hw_registry.c
    src/core/ec_log.c
    src/core/ec_model.c
    src/core/ec_http.c
    src/core/ec_json.c
    src/core/ec_api.c
    src/core/ec_tool.c
    src/core/ec_session.c
    src/core/ec_agent.c
    src/core/ec_io.c
    src/core/ec_capability.c
    src/core/ec_capability_table.c
)

if(EC_PLATFORM STREQUAL "POSIX")
    set(EC_PLATFORM_SOURCES
        src/platform/posix/ec_mmio_posix.c
        src/platform/posix/ec_socket_posix.c
        src/platform/posix/ec_io_posix_uart.c
        src/platform/posix/ec_io_posix_telnet.c
    )
elseif(EC_PLATFORM STREQUAL "FREERTOS")
    set(EC_PLATFORM_SOURCES
        src/platform/freertos/ec_mmio_direct.c
        src/platform/freertos/ec_socket_freertos.c
        src/platform/freertos/ec_io_freertos_uart.c
        src/platform/freertos/ec_io_freertos_telnet.c
        src/platform/freertos/ec_freertos_entry.c
    )
elseif(EC_PLATFORM STREQUAL "BAREMETAL")
    set(EC_PLATFORM_SOURCES
        src/platform/baremetal/ec_mmio_direct.c
        src/platform/baremetal/ec_socket_baremetal.c
        src/platform/baremetal/ec_io_baremetal_uart.c
    )
endif()

set(EC_SOURCES ${EC_CORE_SOURCES} ${EC_PLATFORM_SOURCES})

# Static library
add_library(embedclaw STATIC ${EC_SOURCES})
target_include_directories(embedclaw PUBLIC include)

if(EC_ENABLE_TLS)
    target_compile_definitions(embedclaw PUBLIC EC_CONFIG_USE_TLS=1)
    if(NOT EC_PLATFORM STREQUAL "BAREMETAL")
        target_link_libraries(embedclaw PUBLIC
            MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
    endif()
else()
    target_compile_definitions(embedclaw PUBLIC EC_CONFIG_USE_TLS=0)
endif()

if(EC_HOST_SIM)
    target_compile_definitions(embedclaw PUBLIC EC_CONFIG_HOST_SIM=1)
else()
    target_compile_definitions(embedclaw PUBLIC EC_CONFIG_HOST_SIM=0)
endif()

# Demo executable
if(EC_PLATFORM STREQUAL "POSIX" AND EC_HOST_SIM)
    add_executable(embedclaw_sim_demo src/app/ec_posix_main.c)
    target_link_libraries(embedclaw_sim_demo PRIVATE embedclaw)
elseif(EC_PLATFORM STREQUAL "POSIX")
    add_executable(embedclaw_demo src/app/ec_posix_main.c)
    target_link_libraries(embedclaw_demo PRIVATE embedclaw)
endif()

# -------------------------------------------------------------------------
# End-to-end test executable
#
# Links the full stack EXCEPT ec_http.c (replaced by tests/mock_http.c) and
# ec_socket.c (not needed; mock_http.c provides ec_http_request directly).
# TLS is disabled in tests — the mock replaces the entire HTTP layer.
# -------------------------------------------------------------------------
if(EC_PLATFORM STREQUAL "POSIX")
    set(EC_SOURCES_NO_HTTP
        src/core/ec_hw_access.c
        src/core/ec_hw_registry.c
        src/platform/posix/ec_mmio_posix.c
        src/core/ec_log.c
        src/core/ec_model.c
        src/core/ec_json.c
        src/core/ec_api.c
        src/core/ec_tool.c
        src/core/ec_session.c
        src/core/ec_agent.c
        src/core/ec_io.c
        src/platform/posix/ec_io_posix_uart.c
        src/platform/posix/ec_io_posix_telnet.c
        src/core/ec_capability.c
        src/core/ec_capability_table.c
    )

    add_executable(embedclaw_tests
        ${EC_SOURCES_NO_HTTP}
        tests/mock_http.c
        tests/test_e2e.c
    )
    target_include_directories(embedclaw_tests PRIVATE include tests)
    # Tests use mock HTTP — force TLS off so ec_capability_table.c doesn't try
    # to include mbedTLS headers
    target_compile_definitions(embedclaw_tests PRIVATE EC_CONFIG_USE_TLS=0)

    add_executable(embedclaw_telnet_tests
        src/core/ec_io.c
        src/platform/posix/ec_io_posix_telnet.c
        tests/test_telnet_io.c
    )
    target_include_directories(embedclaw_telnet_tests PRIVATE include tests)
    target_compile_definitions(embedclaw_telnet_tests PRIVATE EC_CONFIG_TELNET_PORT=24232)

    add_executable(embedclaw_unit_tests
        src/core/ec_json.c
        src/core/ec_session.c
        tests/test_unit.c
    )
    target_include_directories(embedclaw_unit_tests PRIVATE include tests)

    add_executable(embedclaw_baremetal_hal_tests
        src/core/ec_io.c
        src/platform/baremetal/ec_io_baremetal_uart.c
        src/platform/baremetal/ec_socket_baremetal.c
        tests/test_baremetal_hal.c
    )
    target_include_directories(embedclaw_baremetal_hal_tests PRIVATE include tests)
    target_compile_definitions(embedclaw_baremetal_hal_tests PRIVATE
        EC_PLATFORM_BAREMETAL
        EC_CONFIG_USE_TLS=0
    )

    if(EC_HOST_SIM)
        add_executable(embedclaw_host_sim_tests
            ${EC_SOURCES_NO_HTTP}
            tests/mock_http.c
            tests/test_host_sim.c
        )
        target_include_directories(embedclaw_host_sim_tests PRIVATE include tests)
        target_compile_definitions(embedclaw_host_sim_tests PRIVATE
            EC_CONFIG_USE_TLS=0
            EC_CONFIG_HOST_SIM=1
        )
    endif()

    enable_testing()
    add_test(NAME e2e COMMAND embedclaw_tests)
    add_test(NAME unit COMMAND embedclaw_unit_tests)
    add_test(NAME baremetal-hal COMMAND embedclaw_baremetal_hal_tests)
    add_test(NAME telnet-io COMMAND embedclaw_telnet_tests)
    if(EC_HOST_SIM)
        add_test(NAME host-sim COMMAND embedclaw_host_sim_tests)
    endif()
endif()
