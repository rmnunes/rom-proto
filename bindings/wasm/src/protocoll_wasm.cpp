// WASM entry point — ensures the C API symbols are linked in.
// The actual API is defined in include/protocoll/protocoll.h and
// compiled into the static library. This file just forces linkage.

#include "protocoll/protocoll.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Keep the module alive (prevent Emscripten from exiting)
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int pcol_wasm_version(void) {
        return 1;
    }

    // Flat wrappers for functions that take PcolEndpoint by value,
    // since Emscripten struct-by-value ABI can be unreliable from JS.
    EMSCRIPTEN_KEEPALIVE
    PcolError pcol_transport_bind_flat(PcolTransport* t, const char* addr, uint16_t port) {
        PcolEndpoint ep = {addr, port};
        return pcol_transport_bind(t, ep);
    }

    EMSCRIPTEN_KEEPALIVE
    PcolError pcol_peer_connect_flat(PcolPeer* peer, const char* addr, uint16_t port) {
        PcolEndpoint ep = {addr, port};
        return pcol_peer_connect(peer, ep);
    }

    EMSCRIPTEN_KEEPALIVE
    void pcol_peer_set_local_endpoint_flat(PcolPeer* peer, const char* addr, uint16_t port) {
        PcolEndpoint ep = {addr, port};
        pcol_peer_set_local_endpoint(peer, ep);
    }

    EMSCRIPTEN_KEEPALIVE
    PcolError pcol_peer_connect_start_flat(PcolPeer* peer, const char* addr, uint16_t port) {
        PcolEndpoint ep = {addr, port};
        return pcol_peer_connect_start(peer, ep);
    }

    EMSCRIPTEN_KEEPALIVE
    PcolError pcol_peer_accept_start_flat(PcolPeer* peer, const char* addr, uint16_t port, int timeout_ms) {
        PcolEndpoint ep = {addr, port};
        return pcol_peer_accept_start(peer, ep, timeout_ms);
    }

    EMSCRIPTEN_KEEPALIVE
    PcolError pcol_peer_accept_flat(PcolPeer* peer, const char* addr, uint16_t port, int timeout_ms) {
        PcolEndpoint ep = {addr, port};
        return pcol_peer_accept(peer, ep, timeout_ms);
    }

    EMSCRIPTEN_KEEPALIVE
    PcolError pcol_peer_connect_to_flat(PcolPeer* peer, uint32_t node_id, const char* addr, uint16_t port) {
        PcolEndpoint ep = {addr, port};
        return pcol_peer_connect_to(peer, node_id, ep);
    }

    EMSCRIPTEN_KEEPALIVE
    PcolError pcol_peer_accept_node_flat(PcolPeer* peer, uint16_t remote_node_id, const char* addr, uint16_t port, int timeout_ms) {
        PcolEndpoint ep = {addr, port};
        return pcol_peer_accept_node(peer, remote_node_id, ep, timeout_ms);
    }
}
#endif
