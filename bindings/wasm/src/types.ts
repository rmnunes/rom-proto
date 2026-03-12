/**
 * Type definitions for the protocoll WASM module.
 */

/** Error codes from the C API. */
export enum ErrorCode {
  OK = 0,
  INVALID = -1,
  NOT_FOUND = -2,
  NO_CONNECT = -3,
  TIMEOUT = -4,
  CRYPTO = -5,
  INTERNAL = -99,
}

/** CRDT types. */
export enum CrdtType {
  LWW_REGISTER = 0,
  G_COUNTER = 1,
  PN_COUNTER = 2,
  OR_SET = 3,
}

/** Reliability levels. */
export enum Reliability {
  RELIABLE = 0,
  BEST_EFFORT = 1,
}

/** Resolution tiers for state synchronization. */
export enum ResolutionTier {
  FULL = 0,
  NORMAL = 1,
  COARSE = 2,
  METADATA = 3,
}

/** Raw WASM module interface (from Emscripten). */
export interface ProtocollModule {
  _malloc(size: number): number;
  _free(ptr: number): void;

  // Key generation
  _pcol_generate_keypair(out: number): void;

  // Transport
  _pcol_transport_loopback_create(bus_id: number): number;
  _pcol_transport_udp_create(): number;
  _pcol_transport_bind(t: number, ep_addr: number, ep_port: number): number;
  _pcol_transport_destroy(t: number): void;

  // Peer lifecycle
  _pcol_peer_create(node_id: number, transport: number, keys: number): number;
  _pcol_peer_destroy(peer: number): void;

  // Identity
  _pcol_peer_node_id(peer: number): number;
  _pcol_peer_public_key(peer: number, out: number): void;
  _pcol_peer_register_key(peer: number, remote_id: number, pk: number): void;

  // Connection
  _pcol_peer_connect(peer: number, addr: number, port: number): number;
  _pcol_peer_accept(peer: number, addr: number, port: number, timeout: number): number;
  _pcol_peer_is_connected(peer: number): number;
  _pcol_peer_disconnect(peer: number): void;
  _pcol_peer_set_local_endpoint(peer: number, addr: number, port: number): void;

  // Multi-connection
  _pcol_peer_connect_to(peer: number, node_id: number, addr: number, port: number): number;
  _pcol_peer_accept_node(peer: number, node_id: number, addr: number, port: number, timeout: number): number;
  _pcol_peer_disconnect_node(peer: number, node_id: number): void;
  _pcol_peer_is_connected_to(peer: number, node_id: number): number;

  // Resolution tiers
  _pcol_peer_set_resolution(peer: number, node_id: number, tier: number): number;

  // Routing
  _pcol_peer_announce_route(peer: number, path: number): number;
  _pcol_peer_learn_route(peer: number, path: number, via_node_id: number): number;
  _pcol_peer_has_route(peer: number, path: number): number;

  // State
  _pcol_declare(peer: number, path: number, crdt: number, rel: number): number;
  _pcol_set_lww(peer: number, path: number, data: number, len: number): number;
  _pcol_get_lww(peer: number, path: number, buf: number, buflen: number, outlen: number): number;
  _pcol_increment_counter(peer: number, path: number, amount_lo: number, amount_hi: number): number;
  _pcol_get_counter(peer: number, path: number, out: number): number;

  // Network I/O
  _pcol_flush(peer: number): number;
  _pcol_poll(peer: number, timeout: number): number;

  // Access control
  _pcol_set_access_control(peer: number, enabled: number): void;

  // Subscription with resolution
  _pcol_subscribe_with_resolution(peer: number, pattern: number, tier: number, initial_credits: number, freshness_us: number): number;

  // API version
  _pcol_api_version(): number;

  // Emscripten helpers
  ccall(name: string, returnType: string, argTypes: string[], args: unknown[]): unknown;
  cwrap(name: string, returnType: string, argTypes: string[]): (...args: unknown[]) => unknown;
  UTF8ToString(ptr: number): string;
  stringToUTF8(str: string, outPtr: number, maxBytes: number): void;
  allocateUTF8(str: string): number;

  // Memory views
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
}

/** Endpoint address. */
export interface Endpoint {
  address: string;
  port: number;
}
