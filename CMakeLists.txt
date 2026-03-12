cmake_minimum_required(VERSION 3.20)
project(protocoll VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform detection
include(cmake/Platform.cmake)
include(cmake/CompilerWarnings.cmake)

# Dependencies via FetchContent
include(FetchContent)

FetchContent_Declare(
    xxHash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.2
)
FetchContent_MakeAvailable(xxHash)

FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG 4.0.2
)
FetchContent_MakeAvailable(monocypher)

# Build Monocypher as a static C library
add_library(monocypher_lib STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c
)
target_include_directories(monocypher_lib PUBLIC
    ${monocypher_SOURCE_DIR}/src
    ${monocypher_SOURCE_DIR}/src/optional
)
set_target_properties(monocypher_lib PROPERTIES C_STANDARD 99)
# Suppress warnings on third-party code
if(MSVC)
    target_compile_options(monocypher_lib PRIVATE /W0)
else()
    target_compile_options(monocypher_lib PRIVATE -w)
endif()

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Optional: DTLS transport encryption (requires mbedTLS)
option(PROTOCOLL_ENABLE_DTLS "Enable DTLS transport encryption via mbedTLS" OFF)
if(PROTOCOLL_ENABLE_DTLS)
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG v3.6.0
    )
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mbedtls)
endif()

# Library
add_library(protocoll
    src/wire/packet.cpp
    src/wire/frame.cpp
    src/wire/codec.cpp
    src/transport/loopback_transport.cpp
    src/transport/udp_transport.cpp
    src/connection/connection.cpp
    src/connection/handshake.cpp
    src/reliability/ack_tracker.cpp
    src/state/state_path.cpp
    src/state/version_vector.cpp
    src/state/crdt/lww_register.cpp
    src/state/crdt/g_counter.cpp
    src/state/crdt/pn_counter.cpp
    src/state/crdt/or_set.cpp
    src/state/state_registry.cpp
    src/state/subscription.cpp
    src/state/observer.cpp
    src/state/resolution.cpp
    src/state/event_log.cpp
    src/reliability/freshness.cpp
    src/connection/recovery.cpp
    src/reliability/congestion.cpp
    src/reliability/fec.cpp
    src/transport/async_transport.cpp
    src/transport/dtls_transport.cpp
    src/transport/iocp_transport.cpp
    src/routing/route_table.cpp
    src/routing/router.cpp
    src/connection/connection_manager.cpp
    src/relay/aggregator.cpp
    src/relay/relay_node.cpp
    src/transport/dpdk_transport.cpp
    src/transport/iouring_transport.cpp
    src/security/crypto.cpp
    src/security/capability.cpp
    src/peer.cpp
    src/protocoll_c_api.cpp
    src/util/hash.cpp
    src/util/clock.cpp
)

target_include_directories(protocoll PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${xxhash_SOURCE_DIR}
)

target_compile_features(protocoll PUBLIC cxx_std_20)
protocoll_set_warnings(protocoll)

target_link_libraries(protocoll PUBLIC monocypher_lib)

if(PROTOCOLL_ENABLE_DTLS)
    target_compile_definitions(protocoll PUBLIC PROTOCOLL_ENABLE_DTLS)
    target_link_libraries(protocoll PUBLIC
        MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto)
endif()

# Optional: io_uring transport (Linux 5.1+ only)
option(PROTOCOLL_ENABLE_IOURING "Enable io_uring async transport (Linux only)" OFF)
if(PROTOCOLL_ENABLE_IOURING)
    FetchContent_Declare(
        liburing
        GIT_REPOSITORY https://github.com/axboe/liburing.git
        GIT_TAG liburing-2.5
    )
    FetchContent_MakeAvailable(liburing)
    target_compile_definitions(protocoll PUBLIC PROTOCOLL_ENABLE_IOURING)
    target_link_libraries(protocoll PUBLIC uring)
endif()

# Optional: DPDK kernel-bypass transport (Linux only)
option(PROTOCOLL_ENABLE_DPDK "Enable DPDK kernel-bypass transport (Linux only)" OFF)
if(PROTOCOLL_ENABLE_DPDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)
    target_compile_definitions(protocoll PUBLIC PROTOCOLL_ENABLE_DPDK)
    target_include_directories(protocoll PUBLIC ${DPDK_INCLUDE_DIRS})
    target_link_libraries(protocoll PUBLIC ${DPDK_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(protocoll PUBLIC ws2_32 bcrypt)
endif()

# Install targets (used by Rust/Python bindings via cmake crate)
install(TARGETS protocoll monocypher_lib
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/protocoll
    DESTINATION include)

# Tests
enable_testing()
add_executable(protocoll_tests
    tests/test_packet.cpp
    tests/test_frame.cpp
    tests/test_codec.cpp
    tests/test_connection.cpp
    tests/test_handshake.cpp
    tests/test_loopback_transport.cpp
    tests/test_ack_tracker.cpp
    tests/test_state_path.cpp
    tests/test_version_vector.cpp
    tests/test_lww_register.cpp
    tests/test_g_counter.cpp
    tests/test_pn_counter.cpp
    tests/test_or_set.cpp
    tests/test_subscription.cpp
    tests/test_freshness.cpp
    tests/test_event_log.cpp
    tests/test_recovery.cpp
    tests/test_congestion.cpp
    tests/test_fec.cpp
    tests/test_async_transport.cpp
    tests/test_crypto.cpp
    tests/test_capability.cpp
    tests/test_observer.cpp
    tests/test_c_api.cpp
    tests/test_cli_decode.cpp
    tests/test_dtls_transport.cpp
    tests/test_resolution.cpp
    tests/test_iocp_transport.cpp
    tests/test_router.cpp
    tests/test_relay_node.cpp
    tests/test_dpdk_transport.cpp
    tests/test_connection_manager.cpp
    tests/test_peer_multi_conn.cpp
    tests/test_mesh_topology.cpp
    tests/test_integration.cpp
    tests/test_main.cpp
)
target_link_libraries(protocoll_tests PRIVATE protocoll GTest::gtest)
target_include_directories(protocoll_tests PRIVATE ${xxhash_SOURCE_DIR})
add_test(NAME protocoll_tests COMMAND protocoll_tests)

# Example
add_executable(state_streaming_demo examples/state_streaming_demo.cpp)
target_link_libraries(state_streaming_demo PRIVATE protocoll)

# CLI Tools
add_executable(protocoll_dump tools/protocoll_dump.cpp)
target_link_libraries(protocoll_dump PRIVATE protocoll)

add_executable(protocoll_inspect tools/protocoll_inspect.cpp)
target_link_libraries(protocoll_inspect PRIVATE protocoll)
