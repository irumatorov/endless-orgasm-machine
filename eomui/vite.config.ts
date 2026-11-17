import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import { viteSingleFile } from 'vite-plugin-singlefile'

// Builds one self-contained index.html into dist/. pack.mjs then copies it, gzipped,
// to ../data/ui for the ESP32 to serve.
export default defineConfig({
  define: {
    global: 'globalThis',
  },
  plugins: [svelte(), viteSingleFile()],
})
