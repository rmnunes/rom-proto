/**
 * BrowserTransport — abstract base class for browser-based transport adapters.
 *
 * Bridges JavaScript networking APIs (WebSocket, WebTransport) with the
 * C++ ExternalTransport via the C API. Subclasses implement sendPacket()
 * using their respective browser API.
 *
 * Data flow:
 *   Browser API receives data → pushRecv() → ExternalTransport recv queue → Peer::poll()
 *   Peer::flush() → ExternalTransport send queue → drainSendQueue() → sendPacket() → Browser API
 */

import type { ProtocollModule } from '../types.js';
import { Transport } from '../index.js';

const MAX_PACKET_SIZE = 65536;

export abstract class BrowserTransport {
  /** The underlying C-side ExternalTransport handle. Pass this to Protocoll.createPeer(). */
  readonly transport: Transport;

  protected _mod: ProtocollModule;
  private _transportPtr: number;

  // Reusable buffers for pop_send (avoid per-call allocation)
  private _sendBuf: number;
  private _outLenPtr: number;
  private _addrBuf: number;
  private _portPtr: number;

  constructor(mod: ProtocollModule) {
    this._mod = mod;

    const ptr = mod._pcol_transport_external_create();
    if (ptr === 0) throw new Error('Failed to create ExternalTransport');
    this.transport = new Transport(mod, ptr);
    this._transportPtr = ptr;

    // Pre-allocate WASM-side buffers for drainSendQueue
    this._sendBuf = mod._malloc(MAX_PACKET_SIZE);
    this._outLenPtr = mod._malloc(8); // size_t
    this._addrBuf = mod._malloc(256);
    this._portPtr = mod._malloc(2); // uint16_t
  }

  /**
   * Push a received packet into the ExternalTransport's recv queue.
   * Subclasses call this when data arrives from the network.
   */
  protected pushRecv(data: Uint8Array, fromAddr: string, fromPort: number): void {
    const dataPtr = this._mod._malloc(data.length);
    this._mod.HEAPU8.set(data, dataPtr);
    const addrPtr = this._mod.allocateUTF8(fromAddr);
    this._mod._pcol_transport_external_push_recv(this._transportPtr, dataPtr, data.length, addrPtr, fromPort);
    this._mod._free(addrPtr);
    this._mod._free(dataPtr);
  }

  /**
   * Drain the send queue — pops all outbound packets and calls sendPacket().
   * Call this after peer.flush() or in the poll loop.
   */
  drainSendQueue(): void {
    while (true) {
      const result = this._mod._pcol_transport_external_pop_send(
        this._transportPtr,
        this._sendBuf, MAX_PACKET_SIZE,
        this._outLenPtr,
        this._addrBuf, 256,
        this._portPtr
      );
      if (result !== 0) break; // PCOL_ERR_NOT_FOUND = queue empty

      const len = this._mod.HEAPU32[this._outLenPtr >> 2];
      const data = new Uint8Array(this._mod.HEAPU8.buffer, this._sendBuf, len).slice();
      const addr = this._mod.UTF8ToString(this._addrBuf);
      const port = this._mod.HEAPU8[this._portPtr] | (this._mod.HEAPU8[this._portPtr + 1] << 8);

      this.sendPacket(data, addr, port);
    }
  }

  /**
   * Send a packet over the network. Subclasses implement this using their
   * respective browser API (WebSocket.send(), WebTransport datagrams, etc.).
   */
  protected abstract sendPacket(data: Uint8Array, toAddr: string, toPort: number): void;

  /** Clean up WASM-side buffers. Subclasses should call super.destroy(). */
  destroy(): void {
    this._mod._free(this._sendBuf);
    this._mod._free(this._outLenPtr);
    this._mod._free(this._addrBuf);
    this._mod._free(this._portPtr);
    this.transport.destroy();
  }
}
