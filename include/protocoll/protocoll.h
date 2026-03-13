#ifndef PROTOCOLL_H
#define PROTOCOLL_H

/*
 * ProtoCol C API — stable FFI surface for language bindings.
 *
 * Every function is prefixed with `pcol_`.
 * Opaque handles hide C++ internals.
 * All strings are null-terminated UTF-8.
 * All buffers are caller-owned unless documented otherwise.
 *
 * Thread safety: a single PcolPeer must not be used from multiple
 * threads concurrently. Different PcolPeer handles are independent.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Build config --- */

#ifdef _WIN32
#  ifdef PROTOCOLL_BUILDING_DLL
#    define PCOL_API __declspec(dllexport)
#  else
#    define PCOL_API
#  endif
#else
#  define PCOL_API __attribute__((visibility("default")))
#endif

/* --- Error codes --- */

typedef enum {
    PCOL_OK             =  0,
    PCOL_ERR_INVALID    = -1,  /* Invalid argument */
    PCOL_ERR_NOT_FOUND  = -2,  /* Path not declared */
    PCOL_ERR_NO_CONNECT = -3,  /* Not connected */
    PCOL_ERR_TIMEOUT    = -4,  /* Operation timed out */
    PCOL_ERR_CRYPTO     = -5,  /* Signature verification failed */
    PCOL_ERR_INTERNAL   = -99, /* Unexpected internal error */
} PcolError;

/* --- Opaque handles --- */

typedef struct PcolTransport PcolTransport;
typedef struct PcolPeer      PcolPeer;

/* --- CRDT types --- */

typedef enum {
    PCOL_LWW_REGISTER = 0,
    PCOL_G_COUNTER    = 1,
    PCOL_PN_COUNTER   = 2,
    PCOL_OR_SET       = 3,
} PcolCrdtType;

/* --- Reliability --- */

typedef enum {
    PCOL_RELIABLE    = 0,
    PCOL_BEST_EFFORT = 1,
} PcolReliability;

/* --- Key types --- */

typedef struct {
    uint8_t bytes[32];
} PcolPublicKey;

typedef struct {
    uint8_t public_key[32];
    uint8_t secret_key[64];
} PcolKeyPair;

/* --- Endpoint --- */

typedef struct {
    const char* address;  /* Null-terminated string, e.g. "127.0.0.1" */
    uint16_t    port;
} PcolEndpoint;

/* --- Callbacks --- */

typedef void (*PcolUpdateCallback)(const char* path, const uint8_t* data,
                                    size_t data_len, void* user_data);

typedef void (*PcolSigFailureCallback)(uint16_t author_id, uint32_t path_hash,
                                        void* user_data);

/* --- Key generation --- */

PCOL_API void pcol_generate_keypair(PcolKeyPair* out);

/* --- Transport --- */

/* Create a loopback transport (for testing / in-process use).
 * bus_id groups transports that can communicate with each other. */
PCOL_API PcolTransport* pcol_transport_loopback_create(uint32_t bus_id);

/* Create a UDP transport. */
PCOL_API PcolTransport* pcol_transport_udp_create(void);

/* Bind a transport to a local endpoint. Returns PCOL_OK on success. */
PCOL_API PcolError pcol_transport_bind(PcolTransport* t, PcolEndpoint ep);

/* Destroy a transport. */
PCOL_API void pcol_transport_destroy(PcolTransport* t);

/* --- External transport (for WASM/browser bridging) --- */

/* Create an external transport. JavaScript feeds packets in via
 * pcol_transport_external_push_recv() and drains outbound packets
 * via pcol_transport_external_pop_send(). */
PCOL_API PcolTransport* pcol_transport_external_create(void);

/* Push a received packet into the external transport's recv queue.
 * Called from JS when data arrives on WebSocket/WebTransport. */
PCOL_API PcolError pcol_transport_external_push_recv(
    PcolTransport* t, const uint8_t* data, size_t len,
    const char* from_addr, uint16_t from_port);

/* Pop a sent packet from the external transport's send queue.
 * buf/buf_len: caller-provided buffer for packet data.
 * out_len: receives actual packet length.
 * to_addr_buf/to_addr_buf_len: receives destination address string.
 * to_port: receives destination port.
 * Returns PCOL_OK if a packet was available, PCOL_ERR_NOT_FOUND if empty. */
PCOL_API PcolError pcol_transport_external_pop_send(
    PcolTransport* t, uint8_t* buf, size_t buf_len, size_t* out_len,
    char* to_addr_buf, size_t to_addr_buf_len, uint16_t* to_port);

/* Number of packets waiting in the send queue. */
PCOL_API size_t pcol_transport_external_send_queue_size(PcolTransport* t);

/* --- DTLS transport (optional, requires PROTOCOLL_ENABLE_DTLS) --- */

/* DTLS configuration for encrypted transport. */
typedef struct {
    int         is_server;         /* Non-zero = server role */
    const char* cert_pem;          /* PEM certificate (null-terminated) */
    const char* key_pem;           /* PEM private key (null-terminated) */
    const char* ca_pem;            /* PEM CA cert for peer verification (or NULL) */
    int         verify_peer;       /* Non-zero = require peer cert */
    uint32_t    handshake_timeout_ms; /* Handshake timeout (default 5000) */
} PcolDtlsConfig;

/* Create a DTLS-encrypted transport wrapping an existing transport.
 * The inner transport must outlive the DTLS transport.
 * Returns NULL if DTLS support is not compiled in. */
PCOL_API PcolTransport* pcol_transport_dtls_create(PcolTransport* inner,
                                                     const PcolDtlsConfig* config);

/* Perform DTLS handshake with remote peer. Must be called before send/recv.
 * Returns PCOL_OK on success, PCOL_ERR_TIMEOUT on timeout. */
PCOL_API PcolError pcol_transport_dtls_handshake(PcolTransport* t,
                                                   PcolEndpoint remote,
                                                   int timeout_ms);

/* --- Peer lifecycle --- */

/* Create a new peer with the given node ID and key pair. */
PCOL_API PcolPeer* pcol_peer_create(uint16_t node_id, PcolTransport* transport,
                                     const PcolKeyPair* keys);

/* Destroy a peer (disconnects if connected). */
PCOL_API void pcol_peer_destroy(PcolPeer* peer);

/* --- Identity --- */

PCOL_API uint16_t pcol_peer_node_id(const PcolPeer* peer);

PCOL_API void pcol_peer_public_key(const PcolPeer* peer, PcolPublicKey* out);

/* Register a remote peer's public key for signature verification. */
PCOL_API void pcol_peer_register_key(PcolPeer* peer, uint16_t remote_node_id,
                                      const PcolPublicKey* pk);

/* --- Connection --- */

PCOL_API PcolError pcol_peer_connect(PcolPeer* peer, PcolEndpoint remote);

PCOL_API PcolError pcol_peer_accept(PcolPeer* peer, PcolEndpoint expected_from,
                                     int timeout_ms);

PCOL_API int pcol_peer_is_connected(const PcolPeer* peer);

PCOL_API void pcol_peer_disconnect(PcolPeer* peer);

PCOL_API void pcol_peer_set_local_endpoint(PcolPeer* peer, PcolEndpoint ep);

/* --- Non-blocking connection (for WASM/browser) --- */

/* Start connection handshake (sends CONNECT, returns immediately).
 * Call pcol_peer_connect_poll() repeatedly to check for completion. */
PCOL_API PcolError pcol_peer_connect_start(PcolPeer* peer, PcolEndpoint remote);

/* Poll for connect completion. Returns PCOL_OK if connected,
 * PCOL_ERR_TIMEOUT if still waiting, PCOL_ERR_INVALID on failure. */
PCOL_API PcolError pcol_peer_connect_poll(PcolPeer* peer);

/* Start accepting connections (non-blocking).
 * Call pcol_peer_accept_poll() repeatedly to check for completion. */
PCOL_API PcolError pcol_peer_accept_start(PcolPeer* peer);

/* Poll for accept completion. Same return codes as connect_poll. */
PCOL_API PcolError pcol_peer_accept_poll(PcolPeer* peer);

/* --- State declaration --- */

PCOL_API PcolError pcol_declare(PcolPeer* peer, const char* path,
                                 PcolCrdtType crdt_type,
                                 PcolReliability reliability);

/* --- State mutation --- */

PCOL_API PcolError pcol_set_lww(PcolPeer* peer, const char* path,
                                 const uint8_t* data, size_t len);

PCOL_API PcolError pcol_increment_counter(PcolPeer* peer, const char* path,
                                           uint64_t amount);

/* --- State reading --- */

/* Read the current value of an LWW register.
 * buf/buf_len: caller-provided buffer.
 * out_len: actual value length (may exceed buf_len if buffer too small).
 * Returns PCOL_OK on success, PCOL_ERR_NOT_FOUND if path not declared. */
PCOL_API PcolError pcol_get_lww(const PcolPeer* peer, const char* path,
                                 uint8_t* buf, size_t buf_len, size_t* out_len);

/* Read the current value of a G-Counter. */
PCOL_API PcolError pcol_get_counter(const PcolPeer* peer, const char* path,
                                     uint64_t* out_value);

/* --- Network I/O --- */

/* Flush pending deltas (sign + encode + send). Returns number of frames sent, or < 0 on error. */
PCOL_API int pcol_flush(PcolPeer* peer);

/* Poll for incoming data (receive + verify + decode + merge).
 * Returns number of state changes applied, or < 0 on error. */
PCOL_API int pcol_poll(PcolPeer* peer, int timeout_ms);

/* --- Callbacks --- */

PCOL_API void pcol_on_update(PcolPeer* peer, PcolUpdateCallback cb, void* user_data);

PCOL_API void pcol_on_signature_failure(PcolPeer* peer, PcolSigFailureCallback cb,
                                         void* user_data);

/* --- Access control --- */

PCOL_API void pcol_set_access_control(PcolPeer* peer, int enabled);

/* --- Multi-connection (mesh topology) --- */

PCOL_API PcolError pcol_peer_connect_to(PcolPeer* peer, uint16_t remote_node_id,
                                          PcolEndpoint remote);

PCOL_API PcolError pcol_peer_accept_node(PcolPeer* peer, uint16_t remote_node_id,
                                           PcolEndpoint expected_from, int timeout_ms);

PCOL_API void pcol_peer_disconnect_node(PcolPeer* peer, uint16_t remote_node_id);

PCOL_API int pcol_peer_is_connected_to(const PcolPeer* peer, uint16_t remote_node_id);

/* --- Resolution tiers --- */

typedef enum {
    PCOL_RESOLUTION_FULL     = 0,
    PCOL_RESOLUTION_NORMAL   = 1,
    PCOL_RESOLUTION_COARSE   = 2,
    PCOL_RESOLUTION_METADATA = 3,
} PcolResolutionTier;

PCOL_API void pcol_peer_set_resolution(PcolPeer* peer, uint16_t remote_node_id,
                                         PcolResolutionTier tier);

/* --- Routing --- */

PCOL_API void pcol_peer_announce_route(PcolPeer* peer, const char* path);

PCOL_API void pcol_peer_learn_route(PcolPeer* peer, uint32_t path_hash,
                                      uint16_t via_node);

PCOL_API int pcol_peer_has_route(const PcolPeer* peer, uint32_t path_hash);

/* --- Subscriptions with resolution --- */

/* Subscribe to a path pattern with a specific resolution tier.
 * initial_credits: backpressure credits (-1 = unlimited).
 * freshness_us: max delta age in microseconds (0 = no limit).
 * Returns subscription ID (> 0) on success, or < 0 on error. */
PCOL_API int32_t pcol_subscribe_with_resolution(PcolPeer* peer, const char* pattern,
                                                  PcolResolutionTier tier,
                                                  int32_t initial_credits,
                                                  uint32_t freshness_us);

/* --- API versioning --- */

/* Returns the API version as a packed integer: (major << 16) | (minor << 8) | patch.
 * Current: 0.1.0 = 0x000100. */
PCOL_API uint32_t pcol_api_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROTOCOLL_H */
