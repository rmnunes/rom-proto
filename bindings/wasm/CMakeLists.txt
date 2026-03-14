cmake_minimum_required(VERSION 3.20)
project(protocoll_wasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Build protocoll as a static library first
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/protocoll_build)

# WASM module with exported C API
add_executable(protocoll_wasm
    src/protocoll_wasm.cpp
)

target_link_libraries(protocoll_wasm PRIVATE rom)

if(EMSCRIPTEN)
    set_target_properties(protocoll_wasm PROPERTIES
        SUFFIX ".js"
    )

    target_link_options(protocoll_wasm PRIVATE
        # Export C API functions
        "SHELL:-s EXPORTED_FUNCTIONS=['_pcol_generate_keypair','_pcol_transport_loopback_create','_pcol_transport_udp_create','_pcol_transport_external_create','_pcol_transport_external_push_recv','_pcol_transport_external_pop_send','_pcol_transport_external_send_queue_size','_pcol_transport_bind','_pcol_transport_bind_flat','_pcol_transport_destroy','_pcol_peer_create','_pcol_peer_destroy','_pcol_peer_node_id','_pcol_peer_public_key','_pcol_peer_register_key','_pcol_peer_connect','_pcol_peer_connect_flat','_pcol_peer_accept','_pcol_peer_accept_flat','_pcol_peer_is_connected','_pcol_peer_disconnect','_pcol_peer_set_local_endpoint','_pcol_peer_set_local_endpoint_flat','_pcol_peer_connect_start','_pcol_peer_connect_start_flat','_pcol_peer_connect_poll','_pcol_peer_accept_start','_pcol_peer_accept_start_flat','_pcol_peer_accept_poll','_pcol_peer_connect_to','_pcol_peer_connect_to_flat','_pcol_peer_accept_node','_pcol_peer_accept_node_flat','_pcol_peer_disconnect_node','_pcol_peer_is_connected_to','_pcol_peer_set_resolution','_pcol_peer_announce_route','_pcol_peer_learn_route','_pcol_peer_has_route','_pcol_declare','_pcol_set_lww','_pcol_get_lww','_pcol_increment_counter','_pcol_get_counter','_pcol_flush','_pcol_poll','_pcol_set_access_control','_pcol_subscribe_with_resolution','_pcol_api_version','_malloc','_free']"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','allocateUTF8']"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_NAME='createProtocoll'"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=16777216"
        # Single-threaded WASM (no pthread)
        "SHELL:-s ENVIRONMENT='web,node'"
        "SHELL:-s EXPORT_ES6=1"
    )
endif()
