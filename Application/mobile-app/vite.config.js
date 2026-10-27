import { defineConfig } from 'vite'
import uni from '@dcloudio/vite-plugin-uni'

export default defineConfig(({ isPreview }) => ({
  plugins: isPreview ? [] : [uni()],
  server: {
    host: '0.0.0.0',
    port: 5173,
    strictPort: true,
    proxy: { '/api/host': { target: 'http://127.0.0.1:8787' } },
  },
  preview: {
    host: '0.0.0.0',
    port: 4173,
    strictPort: true,
    proxy: { '/api/host': { target: 'http://127.0.0.1:8787' } },
  },
}))
