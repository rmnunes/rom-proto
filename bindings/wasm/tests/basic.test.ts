/**
 * Basic tests for protocoll WASM bindings.
 *
 * These tests require the WASM module to be built first:
 *   npm run build:wasm
 *
 * Run with:
 *   npm test
 */

import { describe, test, expect } from '@jest/globals';
import { CrdtType, Reliability, ErrorCode, ProtocollError } from '../src/index.js';

// Type-only tests (no WASM runtime needed)

describe('ErrorCode', () => {
  test('values match C API', () => {
    expect(ErrorCode.OK).toBe(0);
    expect(ErrorCode.INVALID).toBe(-1);
    expect(ErrorCode.NOT_FOUND).toBe(-2);
    expect(ErrorCode.NO_CONNECT).toBe(-3);
    expect(ErrorCode.TIMEOUT).toBe(-4);
    expect(ErrorCode.CRYPTO).toBe(-5);
    expect(ErrorCode.INTERNAL).toBe(-99);
  });
});

describe('CrdtType', () => {
  test('values match C API', () => {
    expect(CrdtType.LWW_REGISTER).toBe(0);
    expect(CrdtType.G_COUNTER).toBe(1);
    expect(CrdtType.PN_COUNTER).toBe(2);
    expect(CrdtType.OR_SET).toBe(3);
  });
});

describe('Reliability', () => {
  test('values match C API', () => {
    expect(Reliability.RELIABLE).toBe(0);
    expect(Reliability.BEST_EFFORT).toBe(1);
  });
});

describe('ProtocollError', () => {
  test('creates with code', () => {
    const err = new ProtocollError(ErrorCode.NOT_FOUND);
    expect(err.code).toBe(ErrorCode.NOT_FOUND);
    expect(err.name).toBe('ProtocollError');
    expect(err.message).toContain('NOT_FOUND');
  });

  test('creates with custom message', () => {
    const err = new ProtocollError(ErrorCode.TIMEOUT, 'handshake timed out');
    expect(err.message).toBe('handshake timed out');
    expect(err.code).toBe(ErrorCode.TIMEOUT);
  });
});

// Integration tests (require WASM build)
// These are skipped when WASM module is not available
describe('WASM Integration', () => {
  test.skip('full lifecycle', async () => {
    // const pcol = await initProtocoll();
    // const keys = pcol.generateKeyPair();
    // expect(keys.publicKey.length).toBe(32);
    //
    // const transport = pcol.createLoopbackTransport(1);
    // transport.bind("127.0.0.1", 9000);
    //
    // const peer = pcol.createPeer(1, transport, keys);
    // expect(peer.nodeId).toBe(1);
    //
    // peer.declare("/test", CrdtType.LWW_REGISTER, Reliability.RELIABLE);
    // peer.setLww("/test", new Uint8Array([1, 2, 3]));
    // const val = peer.getLww("/test");
    // expect(val).toEqual(new Uint8Array([1, 2, 3]));
    //
    // peer.destroy();
  });
});
