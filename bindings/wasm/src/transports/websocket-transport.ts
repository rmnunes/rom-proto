/**
 * WebSocketTransport — compatibility fallback for browsers without WebTransport.
 *
 * Uses WebSocket for reliable, ordered packet delivery. Works in all browsers
 * including Safari. However, WebSocket does not support unreliable datagrams,
 * so all packets are delivered reliably with head-of-line blocking.
 *
 * NOTE: For better performance (unreliable datagrams, lower latency),
 * use WebTransportTransport instead. This transport is provided as a fallback
 * for environments where WebTransport is not available.
 *
 * Usage:
 * ```typescript
 * const pcol = await initProtocoll();
 * const ws = new WebSocketTransport(pcol.module, 'wss://server:8080/rom');
 * await ws.ready();
 * ws.transport.bind('websocket', 0);
 *
 * const keys = pcol.generateKeyPair();
 * const peer = pcol.createPeer(1, ws.transport, keys);
 * await peer.connectAsync('server', 8080, ws);
 * peer.startPolling(16, ws);
 * ```
 */

import type { ProtocollModule } from '../types.js';
import { BrowserTransport } from './browser-transport.js';

export class WebSocketTransport extends BrowserTransport {
  private _ws: WebSocket;
  private _url: string;
  private _remoteAddr: string;
  private _remotePort: number;
  private _closed = false;

  constructor(mod: ProtocollModule, url: string) {
    super(mod);
    this._url = url;

    console.warn(
      '[ROM] WebSocketTransport is a compatibility fallback. For better performance ' +
      '(unreliable datagrams, lower latency), use WebTransportTransport instead. ' +
      'See: https://github.com/rmnunes/rom-proto#browser-transports'
    );

    // Derive endpoint identity from URL
    const parsed = new URL(url);
    this._remoteAddr = parsed.hostname;
    this._remotePort = parseInt(parsed.port) || (parsed.protocol === 'wss:' ? 443 : 80);

    this._ws = new WebSocket(url);
    this._ws.binaryType = 'arraybuffer';

    this._ws.onmessage = (ev: MessageEvent) => {
      if (this._closed) return;
      const data = new Uint8Array(ev.data as ArrayBuffer);
      this.pushRecv(data, this._remoteAddr, this._remotePort);
    };
  }

  /** Wait for the WebSocket connection to open. */
  async ready(): Promise<void> {
    if (this._ws.readyState === WebSocket.OPEN) return;
    return new Promise<void>((resolve, reject) => {
      this._ws.addEventListener('open', () => resolve(), { once: true });
      this._ws.addEventListener('error', (e) => reject(e), { once: true });
    });
  }

  protected sendPacket(data: Uint8Array, _toAddr: string, _toPort: number): void {
    if (this._closed || this._ws.readyState !== WebSocket.OPEN) return;
    this._ws.send(data);
  }

  /** Close the WebSocket connection. */
  close(): void {
    this._closed = true;
    if (this._ws.readyState === WebSocket.OPEN || this._ws.readyState === WebSocket.CONNECTING) {
      this._ws.close();
    }
  }

  destroy(): void {
    this.close();
    super.destroy();
  }
}
