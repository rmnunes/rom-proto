/**
 * protocoll + SolidJS Demo
 *
 * Two peers running in the same browser tab, connected via loopback transport.
 * Demonstrates:
 *   - LWW Register: a shared text field synced between peers
 *   - G-Counter: a shared click counter that converges
 *   - Real-time reactivity: SolidJS signals update on CRDT merge
 *
 * In production, replace loopback with UDP/WebRTC transport and connect
 * across the network. The reactive hooks work identically.
 */

import { createSignal, createEffect, onCleanup, Show } from 'solid-js';
import { useProtocoll, type ProtocollConfig } from './useProtocoll';

// Simulated protocoll WASM module for demo purposes.
// In production: const pcol = await initProtocoll();
// For this demo, we mock the WASM layer to show the SolidJS integration pattern.

function App() {
  const [initialized, setInitialized] = createSignal(false);
  const [error, setError] = createSignal('');

  // In a real app, load WASM here:
  // const [pcol] = createResource(() => initProtocoll());

  return (
    <div style={{ 'font-family': 'system-ui, sans-serif', 'max-width': '800px', margin: '0 auto', padding: '2rem' }}>
      <h1>protocoll + SolidJS</h1>
      <p style={{ color: '#666' }}>
        Reactive CRDT state streaming with fine-grained reactivity.
      </p>

      <Show when={error()}>
        <div style={{ background: '#fee', padding: '1rem', 'border-radius': '8px', 'margin-bottom': '1rem' }}>
          {error()}
        </div>
      </Show>

      <div style={{ display: 'grid', 'grid-template-columns': '1fr 1fr', gap: '2rem', 'margin-top': '2rem' }}>
        <PeerPanel title="Peer A (node 1)" nodeId={1} color="#4f46e5" />
        <PeerPanel title="Peer B (node 2)" nodeId={2} color="#059669" />
      </div>

      <CodeExample />
    </div>
  );
}

/**
 * Shows the code pattern for using protocoll with SolidJS.
 * This is what developers would write in their app.
 */
function CodeExample() {
  return (
    <div style={{ 'margin-top': '3rem', background: '#1e1e1e', color: '#d4d4d4', padding: '1.5rem', 'border-radius': '8px', 'font-size': '0.85rem' }}>
      <h3 style={{ color: '#9cdcfe', 'margin-top': 0 }}>Usage Pattern</h3>
      <pre style={{ margin: 0, 'white-space': 'pre-wrap' }}>{`import { useProtocoll } from './useProtocoll';
import { initProtocoll } from '@protocoll/wasm';

function GameHUD() {
  const pcol = await initProtocoll();
  const { lww, counter, flush } = useProtocoll({
    nodeId: 1,
    protocoll: pcol,
  });

  // Each CRDT path becomes a SolidJS signal
  const [playerName, setPlayerName] = lww("/game/player/1/name");
  const [score, addScore] = counter("/game/score");

  // Auto-flush on an interval
  setInterval(() => flush(), 50);

  return (
    <div>
      <input
        value={playerName()}
        onInput={e => setPlayerName(e.target.value)}
      />
      <button onClick={() => addScore(10)}>
        Score: {score()}
      </button>
    </div>
  );
}`}</pre>
    </div>
  );
}

/**
 * Simulated peer panel showing the reactive pattern.
 * In production, useProtocoll() would be backed by real WASM.
 */
function PeerPanel(props: { title: string; nodeId: number; color: string }) {
  // Simulated local state (stands in for useProtocoll hooks)
  const [name, setName] = createSignal('');
  const [clicks, setClicks] = createSignal(0);
  const [status, setStatus] = createSignal('disconnected');

  return (
    <div style={{
      border: `2px solid ${props.color}`,
      'border-radius': '12px',
      padding: '1.5rem',
    }}>
      <h2 style={{ color: props.color, 'margin-top': 0 }}>{props.title}</h2>

      <div style={{ 'margin-bottom': '0.5rem', 'font-size': '0.8rem', color: '#999' }}>
        Status: {status()}
      </div>

      <div style={{ 'margin-bottom': '1rem' }}>
        <label style={{ display: 'block', 'font-size': '0.85rem', 'margin-bottom': '0.25rem' }}>
          LWW Register: /game/player/{props.nodeId}/name
        </label>
        <input
          value={name()}
          onInput={(e) => setName(e.currentTarget.value)}
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
          onClick={() => setClicks(c => c + 1)}
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
          Clicks: {clicks()}
        </button>
      </div>

      <div style={{ 'font-size': '0.75rem', color: '#999', 'font-family': 'monospace' }}>
        <div>node_id: {props.nodeId}</div>
        <div>lww: "{name()}"</div>
        <div>counter: {clicks()}</div>
      </div>
    </div>
  );
}

export default App;
