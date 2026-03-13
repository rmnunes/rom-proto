import { defineConfig } from 'vite';
import solidPlugin from 'vite-plugin-solid';
import path from 'path';

export default defineConfig({
  plugins: [solidPlugin()],
  build: {
    target: 'esnext',
  },
  resolve: {
    alias: {
      '@rmnunes/rom': path.resolve(__dirname, '../../bindings/wasm/src/index.ts'),
    },
  },
  optimizeDeps: {
    // WASM module loaded dynamically
    exclude: ['@rmnunes/rom'],
  },
});
