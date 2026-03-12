import { defineConfig } from 'vite';
import solidPlugin from 'vite-plugin-solid';

export default defineConfig({
  plugins: [solidPlugin()],
  build: {
    target: 'esnext',
  },
  optimizeDeps: {
    // WASM module loaded dynamically
    exclude: ['@protocoll/wasm'],
  },
});
