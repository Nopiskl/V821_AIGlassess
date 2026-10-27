const path = require('path')
const { defineConfig } = require('../../mobile-app/node_modules/@playwright/test')
const root = path.resolve(__dirname, '../../..')
module.exports = defineConfig({
  testDir: __dirname, testMatch: '*.spec.js', timeout: 45000, workers: 1,
  outputDir: path.join(root, '.host-debug/reports/browser-results'), reporter: 'list',
  use: {
    baseURL: process.env.HOST_TEST_URL || 'http://127.0.0.1:8788', viewport: { width: 390, height: 844 },
    trace: 'retain-on-failure', screenshot: 'only-on-failure',
    launchOptions: { executablePath: path.join(root, '.host-debug/tools/chromium_headless_shell-1248/chrome-headless-shell-linux64/chrome-headless-shell') },
  },
})
