/**
 * protocoll + SolidJS Demo
 *
 * Two peers running in the same browser tab, connected via loopback transport.
 * Demonstrates:
 *   - LWW Register: a shared text field synced between peers
 *   - G-Counter: a shared click counter that converges
 *   - Real-time reactivity: SolidJS signals update on CRDT merge
 *
 * For network usage, replace loopback with WebTransportTransport (recommended)
 * or WebSocketTransport (compatibility fallback). The reactive hooks work
 * identically — only the transport setup changes.
 */

import { createSignal, createResource, Show } from 'solid-js';
import { useProtocoll } from './useProtocoll';
import { initProtocoll } from '@rmnunes/rom';
import type { Protocoll } from '@rmnunes/rom';

function App() {
  const [pcol] = createResource(initProtocoll);

  return (
    <div style={{ 'font-family': 'system-ui, sans-serif', 'max-width': '800px', margin: '0 auto', padding: '2rem' }}>
      <h1>protocoll + SolidJS</h1>
      <p style={{ color: '#666' }}>
        Reactive CRDT state streaming with fine-grained reactivity.
      </p>

      <Show when={pcol.loading}>
        <div style={{ padding: '2rem', 'text-align': 'center', color: '#999' }}>
          Loading WASM module...
        </div>
      </Show>

      <Show when={pcol.error}>
        <div style={{ background: '#fee', padding: '1rem', 'border-radius': '8px', 'margin-bottom': '1rem' }}>
          Failed to load WASM: {String(pcol.error)}
        </div>
      </Show>

      <Show when={pcol()}>
        {(protocoll) => (
          <DemoPanel protocoll={protocoll()} />
        )}
      </Show>

      <CodeExample />
    </div>
  );
}

/**
 * Two-peer loopback demo. Both peers share a loopback bus
 * so CRDT deltas flow between them in the same tab.
 */
function DemoPanel(props: { protocoll: Protocoll }) {
  // Peer A: node 1
  const peerA = useProtocoll({
    nodeId: 1,
    protocoll: props.protocoll,
    mode: 'loopback',
    busId: 1,
    address: 'loopback',
    port: 1,
  });

  // Peer B: node 2, same loopback bus
  const peerB = useProtocoll({
    nodeId: 2,
    protocoll: props.protocoll,
    mode: 'loopback',
    busId: 1,
    address: 'loopback',
    port: 2,
  });

  // Exchange keys so signed deltas can be verified
  peerA.peer.registerPeerKey(2, peerB.keys.publicKey);
  peerB.peer.registerPeerKey(1, peerA.keys.publicKey);

  // Connect peers
  peerA.peer.connect('loopback', 2);
  peerB.peer.accept('loopback', 1);

  // Declare shared CRDT paths on both peers
  const [nameA, setNameA] = peerA.lww("/game/player/name");
  const [nameB, setNameB] = peerB.lww("/game/player/name");
  const [clicksA, incrA] = peerA.counter("/app/clicks");
  const [clicksB, incrB] = peerB.counter("/app/clicks");

  return (
    <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr', gap: '2rem', 'margin-top': '2rem' }}>
      <PeerPanel
        title="Peer A (node 1)" color="#4f46e5" nodeId={1}
        name={nameA} setName={setNameA}
        clicks={clicksA} increment={incrA}
        flush={peerA.flush}
      />
      <PeerPanel
        title="Peer B (node 2)" color="#059669" nodeId={2}
        name={nameB} setName={setNameB}
        clicks={clicksB} increment={incrB}
        flush={peerB.flush}
      />
    </div>
  );
}

function PeerPanel(props: {
  title: string;
  color: string;
  nodeId: number;
  name: () => string;
  setName: (v: string) => void;
  clicks: () => number;
  increment: (amount?: number) => void;
  flush: () => number;
}) {
  return (
    <div style={{
      border: `2px solid ${props.color}`,
      'border-radius': '12px',
      padding: '1.5rem',
    }}>
      <h2 style={{ color: props.color, 'margin-top': 0 }}>{props.title}</h2>

      <div style={{ 'margin-bottom': '1rem' }}>
        <label style={{ display: 'block', 'font-size': '0.85rem', 'margin-bottom': '0.25rem' }}>
          LWW Register: /game/player/name
        </label>
        <input
          value={props.name()}
          onInput={(e) => {
            props.setName(e.currentTarget.value);
            props.flush();
          }}
          placeholder="Type a name..."
          style={{
            width: '100%',
            padding: '0.5rem',
            'border-radius': '6px',
            border: '1px solid #ccc',
            'box-sizing': 'border-box',
          }}
        />
      </div>

      <div style={{ 'margin-bottom': '1rem' }}>
        <label style={{ display: 'block', 'font-size': '0.85rem', 'margin-bottom': '0.25rem' }}>
          G-Counter: /app/clicks
        </label>
        <button
          onClick={() => {
            props.increment();
            props.flush();
          }}
          style={{
            background: props.color,
            color: 'white',
            border: 'none',
            padding: '0.5rem 1.5rem',
            'border-radius': '6px',
            cursor: 'pointer',
            'font-size': '1rem',
          }}
        >
          Clicks: {props.clicks()}
        </button>
      </div>

      <div style={{ 'font-size': '0.75rem', color: '#999', 'font-family': 'monospace' }}>
        <div>node_id: {props.nodeId}</div>
        <div>lww: "{props.name()}"</div>
        <div>counter: {props.clicks()}</div>
      </div>
    </div>
  );
}

/**
 * Shows the code pattern for using protocoll with SolidJS — both
 * loopback (testing) and network (production) modes.
 */
function CodeExample() {
  const [tab, setTab] = createSignal<'loopback' | 'network'>('loopback');

  const loopbackCode = `import { initProtocoll } from '@rmnunes/rom';
import { useProtocoll } from './useProtocoll';

const pcol = await initProtocoll();
const { lww, counter, flush } = useProtocoll({
  nodeId: 1,
  protocoll: pcol,
  // mode: 'loopback' is the default
});

const [playerName, setPlayerName] = lww("/game/player/1/name");
const [score, addScore] = counter("/game/score");`;

  const networkCode = `import { initProtocoll, WebTransportTransport } from '@rmnunes/rom';
import { useProtocoll } from './useProtocoll';

const pcol = await initProtocoll();

// WebTransport (recommended) — low-latency unreliable datagrams over QUIC
const wt = new WebTransportTransport(pcol.module, 'https://server:4433/rom');
await wt.ready();

const { peer, lww, counter } = useProtocoll({
  nodeId: 1,
  protocoll: pcol,
  mode: 'network',
  browserTransport: wt,
});

// Connect to a remote peer
await peer.connectAsync('server', 4433, wt);

const [playerName, setPlayerName] = lww("/game/player/1/name");

// For Safari compatibility, use WebSocketTransport as a fallback:
// import { WebSocketTransport } from '@rmnunes/rom';
// const ws = new WebSocketTransport(pcol.module, 'wss://server:8080/rom');`;

  return (
    <div style={{ 'margin-top': '3rem', background: '#1e1e1e', color: '#d4d4d4', padding: '1.5rem', 'border-radius': '8px', 'font-size': '0.85rem' }}>
      <div style={{ display: 'flex', gap: '0.5rem', 'margin-bottom': '1rem' }}>
        <button
          onClick={() => setTab('loopback')}
          style={{
            background: tab() === 'loopback' ? '#4f46e5' : '#333',
            color: 'white',
            border: 'none',
            padding: '0.4rem 1rem',
            'border-radius': '4px',
            cursor: 'pointer',
          }}
        >
          Loopback (Testing)
        </button>
        <button
          onClick={() => setTab('network')}
          style={{
            background: tab() === 'network' ? '#059669' : '#333',
            color: 'white',
            border: 'none',
            padding: '0.4rem 1rem',
            'border-radius': '4px',
            cursor: 'pointer',
          }}
        >
          Network (Production)
        </button>
      </div>
      <pre style={{ margin: 0, 'white-space': 'pre-wrap' }}>
        {tab() === 'loopback' ? loopbackCode : networkCode}
      </pre>
    </div>
  );
}

export default App;
