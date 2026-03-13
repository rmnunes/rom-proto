/**
 * useProtocoll — SolidJS reactive bindings for protocoll CRDT state.
 *
 * Bridges protocoll's WASM runtime with SolidJS's fine-grained reactivity.
 * Each CRDT state path becomes a SolidJS signal that updates automatically
 * when remote peers push changes.
 *
 * Supports both loopback (in-browser testing) and network transports
 * (WebTransport recommended, WebSocket fallback).
 *
 * Usage:
 *   const { peer, lww, counter, flush } = useProtocoll(config);
 *   const [pos, setPos] = lww("/game/player/1/pos");
 *   const [clicks, incrementClicks] = counter("/app/clicks");
 */

import { createSignal, onCleanup, type Accessor } from 'solid-js';
import type {
  Protocoll,
  Transport,
  BrowserTransport,
} from '@rmnunes/rom';
import { CrdtType, Reliability } from '@rmnunes/rom';

export interface ProtocollConfig {
  /** This peer's node ID */
  nodeId: number;
  /** Pre-initialized protocoll instance (from initProtocoll()) */
  protocoll: Protocoll;
  /** Transport mode. Default: 'loopback'. */
  mode?: 'loopback' | 'network';
  /** Loopback bus ID (for in-browser testing with mode='loopback') */
  busId?: number;
  /** Local bind address */
  address?: string;
  /** Local bind port */
  port?: number;
  /** Poll interval in ms (default: 16 ~= 60fps) */
  pollIntervalMs?: number;
  /** Browser transport instance (for mode='network'). Created externally via WebTransportTransport or WebSocketTransport. */
  browserTransport?: BrowserTransport;
}

type LwwHook = [
  /** Current value as string (reactive) */
  Accessor<string>,
  /** Set a new value */
  (value: string) => void,
];

type CounterHook = [
  /** Current counter value (reactive) */
  Accessor<number>,
  /** Increment the counter */
  (amount?: number) => void,
];

export function useProtocoll(config: ProtocollConfig) {
  const {
    nodeId,
    mode = 'loopback',
    busId = 0,
    address = mode === 'loopback' ? 'loopback' : '0.0.0.0',
    port = nodeId,
    pollIntervalMs = 16,
    protocoll,
    browserTransport,
  } = config;

  // Setup transport and peer
  const keys = protocoll.generateKeyPair();
  let transport: Transport;

  if (mode === 'network' && browserTransport) {
    transport = browserTransport.transport;
  } else {
    transport = protocoll.createLoopbackTransport(busId);
  }

  transport.bind(address, port);
  const peer = protocoll.createPeer(nodeId, transport, keys);

  // Track declared paths and their signals
  const lwwSignals = new Map<string, ReturnType<typeof createSignal<string>>>();
  const counterSignals = new Map<string, ReturnType<typeof createSignal<number>>>();

  // Poll loop — reads incoming state and updates signals
  const pollTimer = setInterval(() => {
    try {
      browserTransport?.drainSendQueue();
      peer.flush();
      const changes = peer.poll(0);
      if (changes > 0) {
        // Refresh all tracked LWW signals
        for (const [path, [, setVal]] of lwwSignals) {
          try {
            const raw = peer.getLww(path);
            setVal(new TextDecoder().decode(raw));
          } catch {
            // path may not have received data yet
          }
        }
        // Refresh all tracked counter signals
        for (const [path, [, setVal]] of counterSignals) {
          try {
            setVal(peer.getCounter(path));
          } catch {
            // path may not have received data yet
          }
        }
      }
    } catch {
      // peer may not be connected yet
    }
  }, pollIntervalMs);

  // Cleanup on component unmount
  onCleanup(() => {
    clearInterval(pollTimer);
    peer.destroy();
  });

  /**
   * Declare and bind an LWW register to a SolidJS signal.
   *
   * ```tsx
   * const [name, setName] = lww("/game/player/1/name");
   * return <input value={name()} onInput={e => setName(e.target.value)} />;
   * ```
   */
  function lww(path: string): LwwHook {
    if (lwwSignals.has(path)) {
      const [get, set] = lwwSignals.get(path)!;
      return [get, (v: string) => {
        const encoded = new TextEncoder().encode(v);
        peer.setLww(path, encoded);
        set(v);
      }];
    }

    peer.declare(path, CrdtType.LWW_REGISTER, Reliability.RELIABLE);
    const [get, set] = createSignal('');
    lwwSignals.set(path, [get, set]);

    const setter = (value: string) => {
      const encoded = new TextEncoder().encode(value);
      peer.setLww(path, encoded);
      set(value);
    };

    return [get, setter];
  }

  /**
   * Declare and bind a G-Counter to a SolidJS signal.
   *
   * ```tsx
   * const [clicks, increment] = counter("/app/clicks");
   * return <button onClick={() => increment()}>Clicks: {clicks()}</button>;
   * ```
   */
  function counter(path: string): CounterHook {
    if (counterSignals.has(path)) {
      const [get] = counterSignals.get(path)!;
      return [get, (amount = 1) => {
        peer.incrementCounter(path, amount);
        counterSignals.get(path)![1](peer.getCounter(path));
      }];
    }

    peer.declare(path, CrdtType.G_COUNTER, Reliability.RELIABLE);
    const [get, set] = createSignal(0);
    counterSignals.set(path, [get, set]);

    const increment = (amount = 1) => {
      peer.incrementCounter(path, amount);
      set(peer.getCounter(path));
    };

    return [get, increment];
  }

  /** Flush all pending deltas to connected peers. */
  function flush(): number {
    return peer.flush();
  }

  return {
    /** The underlying protocoll peer (for advanced usage) */
    peer,
    /** The generated key pair */
    keys,
    /** Bind an LWW register to a SolidJS signal */
    lww,
    /** Bind a G-Counter to a SolidJS signal */
    counter,
    /** Flush pending state to connected peers */
    flush,
  };
}
