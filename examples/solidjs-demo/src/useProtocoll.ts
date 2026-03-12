/**
 * useProtocoll — SolidJS reactive bindings for protocoll CRDT state.
 *
 * Bridges protocoll's WASM runtime with SolidJS's fine-grained reactivity.
 * Each CRDT state path becomes a SolidJS signal that updates automatically
 * when remote peers push changes.
 *
 * Usage:
 *   const { peer, lww, counter, flush } = useProtocoll(config);
 *   const [pos, setPos] = lww("/game/player/1/pos");
 *   const [clicks, incrementClicks] = counter("/app/clicks");
 */

import { createSignal, onCleanup, type Accessor } from 'solid-js';

// These types mirror the WASM bindings — in a real project, import from @protocoll/wasm
interface Protocoll {
  generateKeyPair(): { publicKey: Uint8Array; secretKey: Uint8Array };
  createLoopbackTransport(busId?: number): Transport;
  createPeer(nodeId: number, transport: Transport, keys: any): Peer;
  apiVersion(): number;
}

interface Transport {
  _ptr: number;
  bind(address: string, port: number): void;
  destroy(): void;
}

interface Peer {
  readonly nodeId: number;
  declare(path: string, crdtType: number, reliability?: number): void;
  setLww(path: string, data: Uint8Array): void;
  getLww(path: string): Uint8Array;
  incrementCounter(path: string, amount?: number): void;
  getCounter(path: string): number;
  connect(address: string, port: number): void;
  accept(address: string, port: number, timeoutMs?: number): void;
  flush(): number;
  poll(timeoutMs?: number): number;
  registerPeerKey(remoteNodeId: number, publicKey: Uint8Array): void;
  setLocalEndpoint(address: string, port: number): void;
  destroy(): void;
}

// CRDT type constants (must match C enum)
const LWW_REGISTER = 0;
const G_COUNTER = 1;

export interface ProtocollConfig {
  /** This peer's node ID */
  nodeId: number;
  /** Loopback bus ID (for in-browser testing) */
  busId?: number;
  /** Local bind address */
  address?: string;
  /** Local bind port */
  port?: number;
  /** Poll interval in ms (default: 16 ~= 60fps) */
  pollIntervalMs?: number;
  /** Pre-initialized protocoll instance (from initProtocoll()) */
  protocoll: Protocoll;
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
    busId = 0,
    address = 'loopback',
    port = nodeId,
    pollIntervalMs = 16,
    protocoll,
  } = config;

  // Setup transport and peer
  const keys = protocoll.generateKeyPair();
  const transport = protocoll.createLoopbackTransport(busId);
  transport.bind(address, port);
  const peer = protocoll.createPeer(nodeId, transport, keys);

  // Track declared paths and their signals
  const lwwSignals = new Map<string, ReturnType<typeof createSignal<string>>>();
  const counterSignals = new Map<string, ReturnType<typeof createSignal<number>>>();

  // Poll loop — reads incoming state and updates signals
  const pollTimer = setInterval(() => {
    try {
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

    peer.declare(path, LWW_REGISTER);
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

    peer.declare(path, G_COUNTER);
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
