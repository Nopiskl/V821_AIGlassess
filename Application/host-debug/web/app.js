const screen = document.querySelector('#screen')
const status = document.querySelector('#status')
const pages = document.querySelector('#pages')
const connection = document.querySelector('#connection')
const names = [
  '菜单',
  'AI 助手',
  '实时翻译',
  '手机互联',
  '录音机',
  '相机',
  '文件管理',
  '记事本',
  '文本浏览',
  '设置',
  '应用',
  '小游戏',
  '专注面板',
  '导航',
  '骑行',
  '设备配对',
]
let stopped = false
let frameURL = ''
let lastSequence = -1

for (const [value, text] of names.entries()) pages.add(new Option(text, value))

async function command(body, quiet = false) {
  try {
    const response = await fetch('/api/host/ui', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Aiglass-Host': '1' },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(5000),
    })
    const result = await response.json()
    if (!response.ok || !result.ok) throw new Error(result.error || 'LVGL 未响应')
    screen.dataset.width = result.width
    screen.dataset.height = result.height
    screen.style.aspectRatio = `${result.width} / ${result.height}`
    document.querySelector('#resolution').textContent = `LVGL · ${result.width} × ${result.height}`
    pages.value = String(result.page)
    connection.textContent = result.paired ? '已配对' : '等待配对'
    if (!quiet) status.textContent = 'LVGL 已响应'
    if (result.message_sequence !== lastSequence) {
      lastSequence = result.message_sequence
      document.querySelector('#phone-message').textContent =
        result.last_message || '配对后，在手机端“功能 → 消息投送”发送文字。'
      document.querySelector('#message-state').textContent = result.last_message
        ? 'LVGL 已接收'
        : '等待发送'
    }
    return result
  } catch (error) {
    connection.textContent = '连接中断'
    status.textContent = error.message
    return null
  }
}

pages.onchange = (event) => command({ op: 'open', page: Number(event.target.value) })
for (const button of document.querySelectorAll('[data-key]')) {
  button.onclick = () => command({ op: 'key', key: button.dataset.key })
}
document.querySelector('#edit').onclick = () =>
  command({ op: 'edit', text: document.querySelector('#input').value })
screen.onclick = (event) => {
  const rect = screen.getBoundingClientRect()
  command({
    op: 'tap',
    x: Math.min(
      Number(screen.dataset.width || 640) - 1,
      Math.floor(((event.clientX - rect.left) * Number(screen.dataset.width || 640)) / rect.width),
    ),
    y: Math.min(
      Number(screen.dataset.height || 480) - 1,
      Math.floor(((event.clientY - rect.top) * Number(screen.dataset.height || 480)) / rect.height),
    ),
  })
}
screen.onkeydown = (event) => {
  if (
    ['Enter', 'Escape', 'Tab', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(
      event.key,
    )
  ) {
    event.preventDefault()
    command({ op: 'key', key: event.key })
  }
}

async function pollFrame() {
  if (stopped) return
  try {
    const response = await fetch('/api/host/frame.png', { signal: AbortSignal.timeout(5000) })
    if (!response.ok) throw new Error('LVGL 画面暂不可用')
    const next = URL.createObjectURL(await response.blob())
    screen.src = next
    if (frameURL) URL.revokeObjectURL(frameURL)
    frameURL = next
  } catch (error) {
    status.textContent = error.message
  }
  if (!stopped) setTimeout(pollFrame, 200)
}
async function pollState() {
  if (stopped) return
  await command({ op: 'inspect' }, true)
  if (!stopped) setTimeout(pollState, 500)
}
window.addEventListener('pagehide', () => {
  stopped = true
  if (frameURL) URL.revokeObjectURL(frameURL)
})
pollState()
pollFrame()
