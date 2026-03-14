/**
 * @protocoll/wasm - TypeScript bindings for the protocoll state streaming library.
 *
 * Usage:
 * ```typescript
 * import { createProtocoll, CrdtType, Reliability } from '@protocoll/wasm';
 *
 * const pcol = await createProtocoll();
 * const keys = pcol.generateKeyPair();
 * const transport = pcol.createLoopbackTransport(1);
 * transport.bind("127.0.0.1", 9000);
 *
 * const peer = pcol.createPeer(1, transport, keys);
 * peer.declare("/game/pos", CrdtType.LWW_REGISTER, Reliability.RELIABLE);
 * peer.setLww("/game/pos", new Uint8Array([1, 2, 3]));
 * ```
 */

export { CrdtType, Reliability, ErrorCode, ResolutionTier } from './types.js';
export type { Endpoint, ProtocollModule } from './types.js';
import type { ProtocollModule, Endpoint } from './types.js';
import { CrdtType, Reliability, ErrorCode, ResolutionTier } from './types.js';

// Browser transport adapters
export { BrowserTransport } from './transports/browser-transport.js';
export { WebTransportTransport } from './transports/webtransport-transport.js';
export { WebSocketTransport } from './transports/websocket-transport.js';

/** Custom error class for protocoll errors. */
export class ProtocollError extends Error {
  constructor(public code: ErrorCode, message?: string) {
    super(message ?? `protocoll error: ${ErrorCode[code] ?? code}`);
    this.name = 'ProtocollError';
  }
}

function checkError(code: number): void {
  if (code !== 0) {
    throw new ProtocollError(code as ErrorCode);
  }
}

/** Ed25519 key pair. */
export class KeyPair {
  constructor(
    public readonly publicKey: Uint8Array,
    public readonly secretKey: Uint8Array,
  ) {}
}

/** Transport handle wrapping the C resource. */
export class Transport {
  /** @internal */
  _ptr: number;
  private _mod: ProtocollModule;
  private _destroyed = false;

  /** @internal */
  constructor(mod: ProtocollModule, ptr: number) {
    this._mod = mod;
    this._ptr = ptr;
  }

  /** Bind to a local address and port. */
  bind(address: string, port: number): void {
    const addrPtr = this._mod.allocateUTF8(address);
    // Use flat wrapper to avoid struct-by-value ABI issues in Emscripten
    const code = this._mod._pcol_transport_bind_flat(this._ptr, addrPtr, port);
    this._mod._free(addrPtr);
    checkError(code);
  }

  /** Destroy the transport. */
  destroy(): void {
    if (!this._destroyed) {
      this._mod._pcol_transport_destroy(this._ptr);
      this._destroyed = true;
    }
  }
}

/** Peer handle — high-level API for a protocoll endpoint. */
export class Peer {
  private _ptr: number;
  private _mod: ProtocollModule;
  private _transport: Transport;
  private _destroyed = false;

  /** @internal */
  constructor(mod: ProtocollModule, ptr: number, transport: Transport) {
    this._mod = mod;
    this._ptr = ptr;
    this._transport = transport;
  }

  /** Get this peer's node ID. */
  get nodeId(): number {
    return this._mod._pcol_peer_node_id(this._ptr);
  }

  /** Get this peer's public key. */
  get publicKey(): Uint8Array {
    const outPtr = this._mod._malloc(32);
    this._mod._pcol_peer_public_key(this._ptr, outPtr);
    const key = new Uint8Array(this._mod.HEAPU8.buffer, outPtr, 32).slice();
    this._mod._free(outPtr);
    return key;
  }

  /** Register a remote peer's public key. */
  registerPeerKey(remoteNodeId: number, publicKey: Uint8Array): void {
    const pkPtr = this._mod._malloc(32);
    this._mod.HEAPU8.set(publicKey.subarray(0, 32), pkPtr);
    this._mod._pcol_peer_register_key(this._ptr, remoteNodeId, pkPtr);
    this._mod._free(pkPtr);
  }

  /** Set the local endpoint. */
  setLocalEndpoint(address: string, port: number): void {
    const addrPtr = this._mod.allocateUTF8(address);
    this._mod._pcol_peer_set_local_endpoint_flat(this._ptr, addrPtr, port);
    this._mod._free(addrPtr);
  }

  /** Connect to a remote peer. */
  connect(address: string, port: number): void {
    const addrPtr = this._mod.allocateUTF8(address);
    const code = this._mod._pcol_peer_connect_flat(this._ptr, addrPtr, port);
    this._mod._free(addrPtr);
    checkError(code);
  }

  /** Accept a connection from a remote peer. */
  accept(address: string, port: number, timeoutMs = 5000): void {
    const addrPtr = this._mod.allocateUTF8(address);
    const code = this._mod._pcol_peer_accept_flat(this._ptr, addrPtr, port, timeoutMs);
    this._mod._free(addrPtr);
    checkError(code);
  }

  /** Check if connected. */
  get isConnected(): boolean {
    return this._mod._pcol_peer_is_connected(this._ptr) !== 0;
  }

  /** Disconnect from the remote peer. */
  disconnect(): void {
    this._mod._pcol_peer_disconnect(this._ptr);
  }

  // --- Non-blocking connection (for browser transports) ---

  /** Start a connection handshake (non-blocking). Use with connectPoll() or connectAsync(). */
  connectStart(address: string, port: number): void {
    const addrPtr = this._mod.allocateUTF8(address);
    const code = this._mod._pcol_peer_connect_start_flat(this._ptr, addrPtr, port);
    this._mod._free(addrPtr);
    checkError(code);
  }

  /** Poll for connection completion. Returns true when connected. */
  connectPoll(): boolean {
    return this._mod._pcol_peer_connect_poll(this._ptr) === 0;
  }

  /** Start accepting connections (non-blocking). Use with acceptPoll() or acceptAsync(). */
  acceptStart(): void {
    const code = this._mod._pcol_peer_accept_start(this._ptr);
    checkError(code);
  }

  /** Poll for accept completion. Returns true when connected. */
  acceptPoll(): boolean {
    return this._mod._pcol_peer_accept_poll(this._ptr) === 0;
  }

  /**
   * Async connect — returns a promise that resolves when connected.
   * If a BrowserTransport is used, it drains the send queue automatically.
   */
  async connectAsync(address: string, port: number, browserTransport?: { drainSendQueue(): void }, timeoutMs = 10000): Promise<void> {
    this.connectStart(address, port);
    browserTransport?.drainSendQueue();

    const start = Date.now();
    return new Promise<void>((resolve, reject) => {
      const check = (): void => {
        browserTransport?.drainSendQueue();
        if (this.connectPoll()) {
          resolve();
        } else if (Date.now() - start > timeoutMs) {
          reject(new ProtocollError(ErrorCode.TIMEOUT, 'connectAsync timed out'));
        } else {
          setTimeout(check, 16);
        }
      };
      check();
    });
  }

  /**
   * Async accept — returns a promise that resolves when a connection is accepted.
   */
  async acceptAsync(browserTransport?: { drainSendQueue(): void }, timeoutMs = 10000): Promise<void> {
    this.acceptStart();

    const start = Date.now();
    return new Promise<void>((resolve, reject) => {
      const check = (): void => {
        browserTransport?.drainSendQueue();
        if (this.acceptPoll()) {
          resolve();
        } else if (Date.now() - start > timeoutMs) {
          reject(new ProtocollError(ErrorCode.TIMEOUT, 'acceptAsync timed out'));
        } else {
          setTimeout(check, 16);
        }
      };
      check();
    });
  }

  // --- Polling convenience ---

  private _pollTimer: ReturnType<typeof setInterval> | null = null;

  /** Start a poll loop that calls flush() and poll(0) at the given interval. */
  startPolling(intervalMs = 16, browserTransport?: { drainSendQueue(): void }): void {
    this.stopPolling();
    this._pollTimer = setInterval(() => {
      browserTransport?.drainSendQueue();
      this.flush();
      this.poll(0);
    }, intervalMs);
  }

  /** Stop the poll loop. */
  stopPolling(): void {
    if (this._pollTimer !== null) {
      clearInterval(this._pollTimer);
      this._pollTimer = null;
    }
  }

  /** Declare a state region. */
  declare(path: string, crdtType: CrdtType, reliability: Reliability = Reliability.RELIABLE): void {
    const pathPtr = this._mod.allocateUTF8(path);
    const code = this._mod._pcol_declare(this._ptr, pathPtr, crdtType, reliability);
    this._mod._free(pathPtr);
    checkError(code);
  }

  /** Set an LWW register value. */
  setLww(path: string, data: Uint8Array): void {
    const pathPtr = this._mod.allocateUTF8(path);
    const dataPtr = this._mod._malloc(data.length);
    this._mod.HEAPU8.set(data, dataPtr);
    const code = this._mod._pcol_set_lww(this._ptr, pathPtr, dataPtr, data.length);
    this._mod._free(dataPtr);
    this._mod._free(pathPtr);
    checkError(code);
  }

  /** Read an LWW register value. */
  getLww(path: string): Uint8Array {
    const pathPtr = this._mod.allocateUTF8(path);
    const bufSize = 4096;
    const bufPtr = this._mod._malloc(bufSize);
    const outLenPtr = this._mod._malloc(8); // size_t
    const code = this._mod._pcol_get_lww(this._ptr, pathPtr, bufPtr, bufSize, outLenPtr);
    const outLen = this._mod.HEAPU32[outLenPtr >> 2];
    const result = new Uint8Array(this._mod.HEAPU8.buffer, bufPtr, outLen).slice();
    this._mod._free(outLenPtr);
    this._mod._free(bufPtr);
    this._mod._free(pathPtr);
    checkError(code);
    return result;
  }

  /** Increment a counter. */
  incrementCounter(path: string, amount = 1): void {
    const pathPtr = this._mod.allocateUTF8(path);
    // uint64 split into lo/hi for 32-bit WASM
    const code = this._mod._pcol_increment_counter(this._ptr, pathPtr, amount, 0);
    this._mod._free(pathPtr);
    checkError(code);
  }

  /** Read a counter value. */
  getCounter(path: string): number {
    const pathPtr = this._mod.allocateUTF8(path);
    const outPtr = this._mod._malloc(8);
    const code = this._mod._pcol_get_counter(this._ptr, pathPtr, outPtr);
    const value = this._mod.HEAPU32[outPtr >> 2]; // Low 32 bits
    this._mod._free(outPtr);
    this._mod._free(pathPtr);
    checkError(code);
    return value;
  }

  /** Flush pending deltas. Returns frames sent. */
  flush(): number {
    const result = this._mod._pcol_flush(this._ptr);
    if (result < 0) checkError(result);
    return result;
  }

  /** Poll for incoming data. Returns state changes applied. */
  poll(timeoutMs = 0): number {
    const result = this._mod._pcol_poll(this._ptr, timeoutMs);
    if (result < 0) checkError(result);
    return result;
  }

  /** Enable or disable access control. */
  setAccessControl(enabled: boolean): void {
    this._mod._pcol_set_access_control(this._ptr, enabled ? 1 : 0);
  }

  // --- Multi-connection ---

  /** Connect to a specific remote node. */
  connectTo(nodeId: number, address: string, port: number): void {
    const addrPtr = this._mod.allocateUTF8(address);
    const code = this._mod._pcol_peer_connect_to_flat(this._ptr, nodeId, addrPtr, port);
    this._mod._free(addrPtr);
    checkError(code);
  }

  /** Accept a connection from a specific remote node. */
  acceptNode(nodeId: number, address: string, port: number, timeoutMs = 5000): void {
    const addrPtr = this._mod.allocateUTF8(address);
    const code = this._mod._pcol_peer_accept_node_flat(this._ptr, nodeId, addrPtr, port, timeoutMs);
    this._mod._free(addrPtr);
    checkError(code);
  }

  /** Disconnect from a specific remote node. */
  disconnectNode(nodeId: number): void {
    this._mod._pcol_peer_disconnect_node(this._ptr, nodeId);
  }

  /** Check if connected to a specific remote node. */
  isConnectedTo(nodeId: number): boolean {
    return this._mod._pcol_peer_is_connected_to(this._ptr, nodeId) !== 0;
  }

  // --- Resolution tiers ---

  /** Set the resolution tier for synchronization with a specific node. */
  setResolution(nodeId: number, tier: ResolutionTier): void {
    const code = this._mod._pcol_peer_set_resolution(this._ptr, nodeId, tier);
    checkError(code);
  }

  // --- Routing ---

  /** Announce a route so other peers can discover this path through us. */
  announceRoute(path: string): void {
    const pathPtr = this._mod.allocateUTF8(path);
    const code = this._mod._pcol_peer_announce_route(this._ptr, pathPtr);
    this._mod._free(pathPtr);
    checkError(code);
  }

  /** Learn a route to a path via a specific node. */
  learnRoute(path: string, viaNodeId: number): void {
    const pathPtr = this._mod.allocateUTF8(path);
    const code = this._mod._pcol_peer_learn_route(this._ptr, pathPtr, viaNodeId);
    this._mod._free(pathPtr);
    checkError(code);
  }

  /** Check if a route exists for a given path. */
  hasRoute(path: string): boolean {
    const pathPtr = this._mod.allocateUTF8(path);
    const result = this._mod._pcol_peer_has_route(this._ptr, pathPtr);
    this._mod._free(pathPtr);
    return result !== 0;
  }

  // --- Subscriptions ---

  /** Subscribe to a pattern with a specific resolution tier. Returns subscription ID. */
  subscribeWithResolution(pattern: string, tier: ResolutionTier, initialCredits = -1, freshnessUs = 0): number {
    const patternPtr = this._mod.allocateUTF8(pattern);
    const result = this._mod._pcol_subscribe_with_resolution(this._ptr, patternPtr, tier, initialCredits, freshnessUs);
    this._mod._free(patternPtr);
    if (result < 0) checkError(result);
    return result;
  }

  /** Destroy the peer and its transport. */
  destroy(): void {
    if (!this._destroyed) {
      this._mod._pcol_peer_destroy(this._ptr);
      this._transport.destroy();
      this._destroyed = true;
    }
  }
}

/** Protocoll runtime — factory for transports, peers, and keys. */
export class Protocoll {
  private _mod: ProtocollModule;

  /** @internal */
  constructor(mod: ProtocollModule) {
    this._mod = mod;
  }

  /** Generate a new Ed25519 key pair. */
  generateKeyPair(): KeyPair {
    // PcolKeyPair: 32 bytes public + 64 bytes secret = 96 bytes
    const kpPtr = this._mod._malloc(96);
    this._mod._pcol_generate_keypair(kpPtr);
    const publicKey = new Uint8Array(this._mod.HEAPU8.buffer, kpPtr, 32).slice();
    const secretKey = new Uint8Array(this._mod.HEAPU8.buffer, kpPtr + 32, 64).slice();
    this._mod._free(kpPtr);
    return new KeyPair(publicKey, secretKey);
  }

  /** Create a loopback transport for testing. */
  createLoopbackTransport(busId = 0): Transport {
    const ptr = this._mod._pcol_transport_loopback_create(busId);
    if (ptr === 0) throw new ProtocollError(ErrorCode.INTERNAL, "Failed to create loopback transport");
    return new Transport(this._mod, ptr);
  }

  /** Create a UDP transport for network communication. */
  createUdpTransport(): Transport {
    const ptr = this._mod._pcol_transport_udp_create();
    if (ptr === 0) throw new ProtocollError(ErrorCode.INTERNAL, "Failed to create UDP transport");
    return new Transport(this._mod, ptr);
  }

  /** Create an external transport for browser bridging (WebSocket/WebTransport). */
  createExternalTransport(): Transport {
    const ptr = this._mod._pcol_transport_external_create();
    if (ptr === 0) throw new ProtocollError(ErrorCode.INTERNAL, "Failed to create external transport");
    return new Transport(this._mod, ptr);
  }

  /** @internal Get the raw WASM module (for BrowserTransport adapters). */
  get module(): ProtocollModule { return this._mod; }

  /** Get the packed API version (major<<16 | minor<<8 | patch). */
  apiVersion(): number {
    return this._mod._pcol_api_version();
  }

  /** Create a peer. */
  createPeer(nodeId: number, transport: Transport, keys: KeyPair): Peer {
    // Write key pair to WASM memory
    const kpPtr = this._mod._malloc(96);
    this._mod.HEAPU8.set(keys.publicKey, kpPtr);
    this._mod.HEAPU8.set(keys.secretKey, kpPtr + 32);

    const ptr = this._mod._pcol_peer_create(nodeId, transport._ptr, kpPtr);
    this._mod._free(kpPtr);

    if (ptr === 0) throw new ProtocollError(ErrorCode.INTERNAL, "Failed to create peer");
    return new Peer(this._mod, ptr, transport);
  }
}

/**
 * Initialize the protocoll WASM module.
 *
 * ```typescript
 * const pcol = await initProtocoll();
 * ```
 */
export async function initProtocoll(): Promise<Protocoll> {
  // Dynamic import of the Emscripten-generated module
  // Users must place protocoll_wasm.js and protocoll_wasm.wasm in the right location
  const createModule = (await import('../wasm/protocoll_wasm.js' as string)).default;
  const mod = await createModule() as ProtocollModule;
  return new Protocoll(mod);
}
