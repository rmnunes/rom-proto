"""
CFFI interface definition for protocoll C API.

This module defines the C declarations and loads the shared library.
"""

import cffi
import os
import sys

ffi = cffi.FFI()

# C declarations matching include/protocoll/protocoll.h
ffi.cdef("""
    /* Error codes */
    typedef enum {
        PCOL_OK             =  0,
        PCOL_ERR_INVALID    = -1,
        PCOL_ERR_NOT_FOUND  = -2,
        PCOL_ERR_NO_CONNECT = -3,
        PCOL_ERR_TIMEOUT    = -4,
        PCOL_ERR_CRYPTO     = -5,
        PCOL_ERR_INTERNAL   = -99,
    } PcolError;

    /* Opaque handles */
    typedef struct PcolTransport PcolTransport;
    typedef struct PcolPeer PcolPeer;

    /* CRDT types */
    typedef enum {
        PCOL_LWW_REGISTER = 0,
        PCOL_G_COUNTER    = 1,
        PCOL_PN_COUNTER   = 2,
        PCOL_OR_SET       = 3,
    } PcolCrdtType;

    /* Reliability */
    typedef enum {
        PCOL_RELIABLE    = 0,
        PCOL_BEST_EFFORT = 1,
    } PcolReliability;

    /* Key types */
    typedef struct {
        uint8_t bytes[32];
    } PcolPublicKey;

    typedef struct {
        uint8_t public_key[32];
        uint8_t secret_key[64];
    } PcolKeyPair;

    /* Endpoint */
    typedef struct {
        const char* address;
        uint16_t port;
    } PcolEndpoint;

    /* Callbacks */
    typedef void (*PcolUpdateCallback)(const char* path, const uint8_t* data,
                                        size_t data_len, void* user_data);
    typedef void (*PcolSigFailureCallback)(uint16_t author_id, uint32_t path_hash,
                                            void* user_data);

    /* Key generation */
    void pcol_generate_keypair(PcolKeyPair* out);

    /* Transport */
    PcolTransport* pcol_transport_loopback_create(uint32_t bus_id);
    PcolTransport* pcol_transport_udp_create(void);
    PcolError pcol_transport_bind(PcolTransport* t, PcolEndpoint ep);
    void pcol_transport_destroy(PcolTransport* t);

    /* Peer lifecycle */
    PcolPeer* pcol_peer_create(uint16_t node_id, PcolTransport* transport,
                                const PcolKeyPair* keys);
    void pcol_peer_destroy(PcolPeer* peer);

    /* Identity */
    uint16_t pcol_peer_node_id(const PcolPeer* peer);
    void pcol_peer_public_key(const PcolPeer* peer, PcolPublicKey* out);
    void pcol_peer_register_key(PcolPeer* peer, uint16_t remote_node_id,
                                 const PcolPublicKey* pk);

    /* Connection */
    PcolError pcol_peer_connect(PcolPeer* peer, PcolEndpoint remote);
    PcolError pcol_peer_accept(PcolPeer* peer, PcolEndpoint expected_from,
                                int timeout_ms);
    int pcol_peer_is_connected(const PcolPeer* peer);
    void pcol_peer_disconnect(PcolPeer* peer);
    void pcol_peer_set_local_endpoint(PcolPeer* peer, PcolEndpoint ep);

    /* State declaration */
    PcolError pcol_declare(PcolPeer* peer, const char* path,
                            PcolCrdtType crdt_type, PcolReliability reliability);

    /* State mutation */
    PcolError pcol_set_lww(PcolPeer* peer, const char* path,
                            const uint8_t* data, size_t len);
    PcolError pcol_increment_counter(PcolPeer* peer, const char* path,
                                      uint64_t amount);

    /* State reading */
    PcolError pcol_get_lww(const PcolPeer* peer, const char* path,
                            uint8_t* buf, size_t buf_len, size_t* out_len);
    PcolError pcol_get_counter(const PcolPeer* peer, const char* path,
                                uint64_t* out_value);

    /* Network I/O */
    int pcol_flush(PcolPeer* peer);
    int pcol_poll(PcolPeer* peer, int timeout_ms);

    /* Callbacks */
    void pcol_on_update(PcolPeer* peer, PcolUpdateCallback cb, void* user_data);
    void pcol_on_signature_failure(PcolPeer* peer, PcolSigFailureCallback cb,
                                    void* user_data);

    /* Access control */
    void pcol_set_access_control(PcolPeer* peer, int enabled);

    /* Resolution tiers */
    typedef enum {
        PCOL_RESOLUTION_FULL     = 0,
        PCOL_RESOLUTION_NORMAL   = 1,
        PCOL_RESOLUTION_COARSE   = 2,
        PCOL_RESOLUTION_METADATA = 3,
    } PcolResolutionTier;

    /* Multi-connection */
    PcolError pcol_peer_connect_to(PcolPeer* peer, uint16_t remote_node_id,
                                    PcolEndpoint remote);
    PcolError pcol_peer_accept_node(PcolPeer* peer, uint16_t remote_node_id,
                                     PcolEndpoint expected_from, int timeout_ms);
    void pcol_peer_disconnect_node(PcolPeer* peer, uint16_t remote_node_id);
    int pcol_peer_is_connected_to(const PcolPeer* peer, uint16_t remote_node_id);

    /* Resolution */
    void pcol_peer_set_resolution(PcolPeer* peer, uint16_t remote_node_id,
                                   PcolResolutionTier tier);

    /* Routing */
    void pcol_peer_announce_route(PcolPeer* peer, const char* prefix);
    void pcol_peer_learn_route(PcolPeer* peer, uint32_t path_hash,
                                uint16_t next_hop);
    int pcol_peer_has_route(const PcolPeer* peer, uint32_t path_hash);

    /* Subscription with resolution */
    int32_t pcol_subscribe_with_resolution(PcolPeer* peer, const char* pattern,
                                            PcolResolutionTier tier,
                                            int32_t initial_credits,
                                            uint32_t freshness_us);

    /* API version */
    uint32_t pcol_api_version(void);
""")


def _find_library():
    """Find the protocoll shared/static library."""
    # Search paths relative to this file, then in standard locations
    this_dir = os.path.dirname(os.path.abspath(__file__))
    search_dirs = [
        this_dir,
        os.path.join(this_dir, ".."),
        os.path.join(this_dir, "..", "..", "..", "build", "Release"),
        os.path.join(this_dir, "..", "..", "..", "build", "Debug"),
        os.path.join(this_dir, "..", "..", "..", "build"),
    ]

    if sys.platform == "win32":
        lib_names = ["protocoll.dll", "protocoll.lib"]
    else:
        lib_names = ["libprotocoll.so", "libprotocoll.dylib"]

    for d in search_dirs:
        for name in lib_names:
            path = os.path.join(d, name)
            if os.path.exists(path):
                return path

    raise RuntimeError(
        "Could not find protocoll library. Build the C++ library first "
        "or set PROTOCOLL_LIB_PATH environment variable."
    )


# Allow override via environment variable
_lib_path = os.environ.get("PROTOCOLL_LIB_PATH", None)
if _lib_path is None:
    _lib_path = _find_library()

lib = ffi.dlopen(_lib_path)
