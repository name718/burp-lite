import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import electron from 'vite-plugin-electron'
import renderer from 'vite-plugin-electron-renderer'

export default defineConfig({
  plugins: [
    vue(),
    electron([
      {
        // Main process entry
        entry: 'electron/main.js',
      },
      {
        // Preload script entry
        entry: 'electron/preload.js',
        onstart(options) {
          // Reload renderer when preload changes
          options.reload()
        },
      },
    ]),
    renderer(),
  ],
  server: {
    port: 3001,
  },
})
