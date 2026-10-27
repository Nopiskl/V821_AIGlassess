const { test, expect } = require('../../mobile-app/node_modules/@playwright/test')
const fs = require('fs/promises')
const path = require('path')
const root = path.resolve(__dirname, '../../..')
const runtime = process.env.HOST_TEST_RUNTIME || path.join(root, '.host-debug/browser-tests')
const button = (page, text) =>
  page.locator('uni-button').filter({ hasText: new RegExp(`^\\s*${text}\\s*$`) })
const call = async (request, channel, body) => {
  const response = await request.post(`/api/host/${channel}`, {
    headers: { 'X-Aiglass-Host': '1' },
    data: body,
  })
  const result = await response.json()
  expect(result.ok, JSON.stringify(result)).toBe(true)
  return result
}
async function connect(page) {
  await page.goto('/')
  await button(page, '查找设备').click()
  await page.locator('uni-input[aria-label="配对码"] input').fill(page.pairingCode)
  await button(page, '确认配对').click()
  await button(page, '进入主页面').click()
  await expect(page.locator('.device-hero')).toBeVisible()
  await expect(page.locator('.demo-badge')).toHaveText('主机联合调试')
}
const fileRow = (page, name) => page.locator('.files-list .list-row').filter({ hasText: name })
test.beforeEach(async ({ page, request }) => {
  page.errors = []
  page.on('pageerror', (error) => page.errors.push(error.message))
  await call(request, 'ui', { op: 'open', page: 15 })
  await expect
    .poll(async () =>
      (await call(request, 'ui', { op: 'inspect' })).labels.some((label) =>
        ['解除配对', '刷新配对码'].includes(label),
      ),
    )
    .toBe(true)
  let state = await call(request, 'ui', { op: 'inspect' })
  if (state.paired) {
    await call(request, 'ui', { op: 'click', text: '解除配对' })
    await expect
      .poll(async () => (await call(request, 'ui', { op: 'inspect' })).labels)
      .toContain('确认解除')
    await call(request, 'ui', { op: 'click', text: '确认解除' })
  }
  await expect.poll(async () => (await call(request, 'ui', { op: 'inspect' })).paired).toBe(false)
  await expect
    .poll(async () =>
      (await call(request, 'ui', { op: 'inspect' })).labels.some((label) => /^\d{6}$/.test(label)),
    )
    .toBe(true)
  state = await call(request, 'ui', { op: 'inspect' })
  page.pairingCode = state.labels.find((label) => /^\d{6}$/.test(label))
  expect(page.pairingCode).toBeTruthy()
})
test.afterEach(async ({ page }) => {
  expect(page.errors).toEqual([])
})

test('phone imports real bytes, uploads with pause/resume, then renames and deletes the host file', async ({
  page,
}) => {
  await connect(page)
  await page.goto('/#/pages/files/index')
  const filename = `browser-${Date.now()}&中文.bin`
  const content = Buffer.alloc(128 * 1024)
  for (let i = 0; i < content.length; i++) content[i] = i % 251
  const chooser = page.waitForEvent('filechooser')
  await button(page, '导入手机文件').click()
  await (
    await chooser
  ).setFiles({ name: filename, mimeType: 'application/octet-stream', buffer: content })
  await fileRow(page, filename).click()
  await button(page, '传到眼镜').click()
  await button(page, '暂停').click()
  await expect(page.locator('.task-status')).toContainText('已暂停')
  await page.reload()
  await expect(page.locator('.task-status')).toContainText('已暂停')
  await button(page, '继续').click()
  await button(page, '已结束.*').click()
  await expect(page.locator('.task-status')).toContainText('已完成', { timeout: 30000 })
  expect(await fs.readFile(path.join(runtime, 'data/inbox', filename))).toEqual(content)
  await button(page, '查看文件').click()
  await fileRow(page, filename).click()
  await button(page, '重命名').click()
  const renamed = `renamed-${filename}`
  await page.locator('uni-input[aria-label="新文件名"] input').fill(renamed)
  await button(page, '保存名称').click()
  await expect(page.locator('.detail-name')).toHaveText(renamed)
  expect(await fs.readFile(path.join(runtime, 'data/inbox', renamed))).toEqual(content)
  await page.goto('/#/pages/home/index')
  await page.locator('.recent-card .list-row').filter({ hasText: renamed }).click()
  await expect(page.locator('.detail-name')).toHaveText(renamed)
  await button(page, '删除').click()
  await page.locator('.uni-modal__btn_primary').click()
  await expect(fileRow(page, renamed)).toHaveCount(0)
  await expect(fs.access(path.join(runtime, 'data/inbox', renamed))).rejects.toThrow()
})

test('LVGL edits a note; phone downloads it and exports identical content', async ({
  page,
  request,
}) => {
  const text = `LVGL 原生编辑器创建的笔记 ${Date.now()}`
  await call(request, 'ui', { op: 'open', page: 7 })
  await expect
    .poll(async () => (await call(request, 'ui', { op: 'inspect' })).labels.join('\n'))
    .toContain('note.txt')
  await call(request, 'ui', { op: 'click', text: '编辑' })
  await call(request, 'ui', { op: 'edit', text })
  await call(request, 'ui', { op: 'click', text: '完成' })
  await expect
    .poll(async () => (await call(request, 'ui', { op: 'inspect' })).labels.join('\n'))
    .toContain(text)
  await call(request, 'ui', { op: 'click', text: '保存' })
  await expect
    .poll(async () => fs.readFile(path.join(runtime, 'data/notes/note.txt'), 'utf8'))
    .toBe(text)
  await connect(page)
  await page.goto('/#/pages/files/index')
  await fileRow(page, 'note.txt').click()
  await expect(page.locator('.text-preview')).toHaveText(text)
  await button(page, '传到手机').click()
  await button(page, '已结束.*').click()
  await expect(page.locator('.task-status')).toContainText('已完成')
  await button(page, '查看文件').click()
  await fileRow(page, 'note.txt').click()
  const download = page.waitForEvent('download')
  await button(page, '导出文件').click()
  const chunks = []
  for await (const chunk of await (await download).createReadStream()) chunks.push(chunk)
  expect(Buffer.concat(chunks).toString()).toBe(text)
})

test('phone notes, notifications, drafts and navigation arrive in the real LVGL process', async ({
  page,
  request,
}) => {
  await connect(page)
  const unique = String(Date.now())
  for (const [kind, label, text, targetPage] of [
    ['note', '同步到眼镜笔记', `手机笔记-${unique}`, 6],
    ['notify', '发送提醒', `手机提醒-${unique}`, 3],
    ['chat', '发送对话草稿', `对话草稿-${unique}`, null],
    ['translate', '发送翻译草稿', `翻译草稿-${unique}`, null],
    ['navigate', '发送路线文字', `前方 200 米右转-${unique}`, 13],
  ]) {
    if (targetPage != null) await call(request, 'ui', { op: 'open', page: targetPage })
    await page.goto('/#/pages/features/index')
    const title = {
      note: '笔记同步',
      notify: '消息投送',
      chat: 'AI 对话草稿',
      translate: '翻译草稿',
      navigate: '导航卡片',
    }[kind]
    await page.locator('.feature-list .list-row').filter({ hasText: title }).click()
    await page.locator('textarea').fill(text)
    await button(page, label).click()
    await expect(page.locator('.recent-messages')).toContainText(text)
    if (kind !== 'note')
      await expect
        .poll(async () => (await call(request, 'ui', { op: 'inspect' })).labels.join('\n'))
        .toContain(text)
    else {
      const files = await fs.readdir(path.join(runtime, 'data/notes'))
      const values = await Promise.all(
        files
          .filter((f) => !f.startsWith('.'))
          .map((f) => fs.readFile(path.join(runtime, 'data/notes', f), 'utf8')),
      )
      expect(values).toContain(text)
    }
  }
  await page.goto('/#/pages/control/index')
  await expect(page.locator('.notice.error')).toContainText('未接入相机和麦克风')
  await expect(page.locator('.shutter')).toHaveAttribute('disabled', 'true')
})

test('first pairing, reconnect, below-frame messages and revocation use only native LVGL', async ({
  page,
  request,
}) => {
  await page.goto('/#/pages/files/index')
  await expect(button(page, '查找设备')).toBeVisible()
  await expect(page.locator('.test-entry')).toHaveCount(0)
  await button(page, '查找设备').click()
  const field = page.locator('uni-input[aria-label="配对码"] input')
  await field.fill(page.pairingCode === '000000' ? '111111' : '000000')
  await button(page, '确认配对').click()
  await expect(page.locator('.form-error')).toContainText('配对码不正确')
  await field.fill(page.pairingCode)
  await button(page, '确认配对').click()
  await button(page, '进入主页面').click()
  await expect(page.locator('.device-hero')).toBeVisible()
  await page.reload()
  await expect(page.locator('.device-hero')).toBeVisible()
  await expect(page.locator('.offline-bar')).toHaveCount(0)
  await page.goto('/#/pages/device/index')
  await button(page, '断开连接').click()
  await page.locator('.uni-modal__btn_primary').click()
  await expect(page.locator('.offline-bar')).toBeVisible()
  await button(page, '重新连接').click()
  await expect(page.locator('.offline-bar')).toHaveCount(0)
  const message = '手机消息：下午三点出发。\n携带相机与充电器。<b>原样文字</b>'
  await page.goto('/#/pages/compose/index?kind=notify')
  await page.locator('textarea').fill(message)
  await button(page, '发送提醒').click()
  await expect(page.locator('.recent-messages')).toContainText('手机消息：下午三点出发。')
  await call(request, 'ui', { op: 'open', page: 0 })
  await expect
    .poll(async () => (await call(request, 'ui', { op: 'inspect' })).last_message)
    .toBe(message)
  await page.goto('/debug/')
  await page.setViewportSize({ width: 1200, height: 950 })
  await expect.poll(() => page.locator('#screen').evaluate((image) => image.naturalWidth)).toBe(640)
  await expect
    .poll(() => page.locator('#screen').evaluate((image) => image.naturalHeight))
    .toBe(480)
  await expect(page.locator('#phone-message')).toHaveText(message)
  await expect(page.locator('#phone-message b')).toHaveCount(0)
  const frame = await page.locator('#screen').boundingBox()
  const text = await page.locator('#phone-message').boundingBox()
  expect(text.y).toBeGreaterThan(frame.y + frame.height)
  await page.screenshot({
    path: path.join(root, '.host-debug/reports/joint-debug-640x480.png'),
    fullPage: true,
  })
  await call(request, 'ui', { op: 'open', page: 15 })
  await expect
    .poll(async () => (await call(request, 'ui', { op: 'inspect' })).labels)
    .toContain('解除配对')
  await call(request, 'ui', { op: 'click', text: '解除配对' })
  await expect
    .poll(async () => (await call(request, 'ui', { op: 'inspect' })).labels)
    .toContain('确认解除')
  await call(request, 'ui', { op: 'click', text: '确认解除' })
  await page.goto('/#/pages/home/index')
  await expect(button(page, '查找设备')).toBeVisible({ timeout: 10000 })
  await expect(page.locator('.test-entry')).toHaveCount(0)
})

test('interrupted real upload reports failure and retries; transport outage recovers', async ({
  page,
}) => {
  await connect(page)
  await page.goto('/#/pages/files/index')
  const filename = `retry-${Date.now()}.bin`,
    content = Buffer.alloc(16 * 1024, 79)
  const chooser = page.waitForEvent('filechooser')
  await button(page, '导入手机文件').click()
  await (
    await chooser
  ).setFiles({ name: filename, mimeType: 'application/octet-stream', buffer: content })
  await fileRow(page, filename).click()
  let interrupted = false
  await page.route('**/api/host/rpc', async (route) => {
    const body = route.request().postDataJSON()
    if (!interrupted && body.op === 'put' && body.offset >= 4096) {
      interrupted = true
      await route.fulfill({
        status: 503,
        contentType: 'application/json',
        body: JSON.stringify({ ok: false, error: '测试传输中断' }),
      })
    } else await route.continue()
  })
  await button(page, '传到眼镜').click()
  await expect(page.locator('.task-status')).toContainText('传输失败')
  await expect(fs.access(path.join(runtime, 'data/inbox', filename))).rejects.toThrow()
  expect((await fs.stat(path.join(runtime, 'data/inbox', `.incoming-${filename}.part`))).size).toBe(
    4096,
  )
  await page.unroute('**/api/host/rpc')
  await button(page, '重试').click()
  await button(page, '已结束.*').click()
  await expect(page.locator('.task-status')).toContainText('已完成')
  expect(await fs.readFile(path.join(runtime, 'data/inbox', filename))).toEqual(content)
  await page.route('**/api/host/rpc', (route) =>
    route.fulfill({
      status: 503,
      contentType: 'application/json',
      body: '{"ok":false,"error":"测试连接中断"}',
    }),
  )
  await expect(page.locator('.offline-bar')).toBeVisible({ timeout: 10000 })
  await page.unroute('**/api/host/rpc')
  await expect(page.locator('.offline-bar')).toHaveCount(0, { timeout: 10000 })
})

// These pages now share the authenticated transport and have no mock entry.
test('paired companion pages keep their layout and local preferences', async ({ page }) => {
  await page.goto('/')
  await expect(button(page, '查找设备')).toBeVisible()
  await page.screenshot({
    path: path.join(root, '.host-debug/reports/mobile-welcome.png'),
    fullPage: true,
  })
  await button(page, '查找设备').click()
  await expect(page.locator('uni-input[aria-label="配对码"] input')).toBeVisible()
  await page.screenshot({
    path: path.join(root, '.host-debug/reports/mobile-setup.png'),
    fullPage: true,
  })
  await page.locator('uni-input[aria-label="配对码"] input').fill(page.pairingCode)
  await button(page, '确认配对').click()
  await button(page, '进入主页面').click()
  for (const name of ['home', 'features', 'store', 'me', 'files']) {
    await page.goto(`/#/pages/${name}/index`)
    await expect(page.locator('.page-title')).toBeVisible()
    await expect(page.locator('.test-entry')).toHaveCount(0)
    await page.screenshot({
      path: path.join(root, `.host-debug/reports/mobile-${name}.png`),
      fullPage: true,
    })
    const width = await page.evaluate(() => document.documentElement.scrollWidth)
    expect(width).toBeLessThanOrEqual(390)
  }
  await page.goto('/#/pages/me/index')
  await button(page, '深色').click()
  await page.reload()
  await expect(page.locator('.app-shell')).toHaveClass(/theme-dark/)
  await button(page, '浅色').click()
  await page.setViewportSize({ width: 320, height: 740 })
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(320)
})
