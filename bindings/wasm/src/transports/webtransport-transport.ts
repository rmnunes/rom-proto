/**
 * WebTransportTransport — ROM's recommended browser transport.
 *
 * Uses WebTransport unreliable datagrams over QUIC for low-latency
 * CRDT delta streaming. This is the best transport for real-time
 * state synchronization in browsers.
 *
 * Browser support: Chrome, Edge, Firefox. Safari expected mid-to-late 2026.
 * For Safari compatibility, use WebSocketTransport as a fallback.
 *
 * Usage:
 * ```typescript
 * const pcol = await initProtocoll();
 * const wt = new WebTransportTransport(pcol.module, 'https://server:4433/rom');
 * await wt.ready();
 * wt.transport.bind('webtransport', 0);
 *
 * const keys = pcol.generateKeyPair();
 * const peer = pcol.createPeer(1, wt.transport, keys);
 * await peer.connectAsync('server', 4433, wt);
 * peer.startPolling(16, wt);
 * ```
 */

import type { ProtocollModule } from '../types.js';
import { BrowserTransport } from './browser-transport.js';

export class WebTransportTransport extends BrowserTransport {
  private _wt: WebTransport;
  private _url: string;
  private _remoteAddr: string;
  private _remotePort: number;
  private _receiving = false;
  private _closed = false;

  constructor(mod: ProtocollModule, url: string) {
    super(mod);
    this._url = url;
    this._wt = new WebTransport(url);

    // Derive endpoint identity from URL
    const parsed = new URL(url);
    this._remoteAddr = parsed.hostname;
    this._remotePort = parseInt(parsed.port) || (parsed.protocol === 'https:' ? 443 : 80);
  }

  /** Wait for the WebTransport session to be ready, then start receiving datagrams. */
  async ready(): Promise<void> {
    await this._wt.ready;
    this._startReceiving();
  }

  private async _startReceiving(): Promise<void> {
    if (this._receiving) return;
    this._receiving = true;

    try {
      const reader = this._wt.datagrams.readable.getReader();
      while (!this._closed) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) {
          this.pushRecv(value, this._remoteAddr, this._remotePort);
        }
      }
      reader.releaseLock();
    } catch {
      // Session closed or errored — stop receiving
    }
  }

  protected sendPacket(data: Uint8Array, _toAddr: string, _toPort: number): void {
    if (this._closed) return;
    const writer = this._wt.datagrams.writable.getWriter();
    writer.write(data).finally(() => writer.releaseLock());
  }

  /** Close the WebTransport session. */
  close(): void {
    this._closed = true;
    try { this._wt.close(); } catch { /* already closed */ }
  }

  destroy(): void {
    this.close();
    super.destroy();
  }
}
