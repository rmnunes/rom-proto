# Platform detection and configuration

if(EMSCRIPTEN)
    add_compile_definitions(PROTOCOLL_PLATFORM_WASM)
    set(PROTOCOLL_PLATFORM_LIBS "")
elseif(WIN32)
    add_compile_definitions(PROTOCOLL_PLATFORM_WINDOWS)
    # Winsock2 for UDP transport
    set(PROTOCOLL_PLATFORM_LIBS ws2_32)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(PROTOCOLL_PLATFORM_LINUX)
    set(PROTOCOLL_PLATFORM_LIBS "")
elseif(APPLE)
    add_compile_definitions(PROTOCOLL_PLATFORM_MACOS)
    set(PROTOCOLL_PLATFORM_LIBS "")
endif()

message(STATUS "ProtoCol platform: ${CMAKE_SYSTEM_NAME}")
