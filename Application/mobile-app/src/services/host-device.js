import { blobAction } from './blob-store.js'
import { MAX_FILE_BYTES } from './limits.js'

const KEY = 'v821-companion.host.v1'
const clone = (value) => JSON.parse(JSON.stringify(value))
const bytes = (value) => new TextEncoder().encode(value).length
const fail = (message) => {
  throw new Error(message)
}
const kindOf = (name) =>
  /\.(png|jpe?g|webp|svg)$/i.test(name)
    ? 'photo'
    : /\.(mp4|webm)$/i.test(name)
      ? 'video'
      : /\.(wav|mp3|opus)$/i.test(name)
        ? 'audio'
        : 'note'
const date = () => new Date().toLocaleString('sv-SE').slice(0, 16)
const id = () => `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`
const hex = (data) => Array.from(data, (n) => n.toString(16).padStart(2, '0')).join('')
function unhex(value) {
  if (
    typeof value !== 'string' ||
    value.length > 8192 ||
    value.length % 2 ||
    /[^\da-f]/i.test(value)
  )
    fail('文件块格式无效')
  return Uint8Array.from(value.match(/../g) || [], (pair) => parseInt(pair, 16))
}
export async function hostRPC(body) {
  let response
  try {
    response = await fetch('/api/host/rpc', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'X-Aiglass-Host': '1' },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(8000),
    })
  } catch {
    fail('无法连接主机 LVGL，请检查联合调试服务')
  }
  let result
  try {
    result = await response.json()
  } catch {
    fail('主机桥接未启动或代理未配置')
  }
  if (!response.ok || !result.ok) {
    const reason = {
      '-17': '目标位置已有同名文件',
      '-2': '文件或目录不存在',
      '-95': '当前主机不支持此功能',
      '-22': '参数或传输偏移无效',
      '-27': '文件超过大小限制',
      '-11': body.op === 'pair' ? '尝试次数过多，请 30 秒后重试' : 'LVGL 消息队列已满，请稍后重试',
      '-13': body.op === 'pair' ? '配对码不正确，请查看眼镜屏幕' : '配对已失效，请重新配对',
      '-114': '设备已绑定，请先在眼镜端解除配对',
    }[String(result.code)]
    const error = new Error(reason || result.error || 'LVGL 请求失败')
    error.code = result.code
    throw error
  }
  return result
}

export function createHostDevice({ storage }) {
  let saved
  try {
    saved = JSON.parse(storage.getItem(KEY) || 'null')
  } catch {
    /* Start with empty host data. */
  }
  let binding =
    saved?.binding &&
    /^[a-f0-9]{32}$/.test(saved.binding.deviceId) &&
    /^[a-f0-9]{64}$/.test(saved.binding.credential)
      ? saved.binding
      : null
  const state = {
    device: binding ? saved?.device || null : null,
    connected: false,
    settings: {
      nickname: '探索者',
      theme: 'light',
      notifications: true,
      compact: false,
      ...saved?.settings,
    },
    files: (saved?.files || []).filter((f) => f.location === 'phone'),
    transfers: (saved?.transfers || []).map((t) =>
      ['running', 'queued'].includes(t.status)
        ? { ...t, status: 'paused', reason: '页面已重新加载，点击继续' }
        : t,
    ),
    messages: saved?.messages || [],
    savedApps: saved?.savedApps || [],
    storageWarning: '',
    capabilities: {},
  }
  const listeners = new Set()
  let polling = false,
    lastPoll = 0,
    working = false,
    workingTask = null,
    enabled = false,
    session = 0
  const emit = () => {
    try {
      storage.setItem(KEY, JSON.stringify({ ...state, binding }))
    } catch {
      state.storageWarning = '本地存储不足，任务记录可能无法保存'
    }
    for (const listener of listeners) listener(clone(state))
  }
  const connected = () => {
    if (!state.connected || !enabled) fail('主机 LVGL 未连接')
  }
  const file = (fileId) => state.files.find((f) => f.id === fileId) || fail('文件已不存在')
  const name = (value) => {
    value = String(value).trim()
    if (!value || value.startsWith('.') || /[\\/\x00-\x1f]/.test(value) || bytes(value) > 120)
      fail('文件名须为 1–120 字节且不能包含路径或控制字符')
    return value
  }
  const busy = (fileId) =>
    state.transfers.some((t) => t.fileId === fileId && !['done', 'cancelled'].includes(t.status))
  function forgetBinding() {
    binding = null
    enabled = false
    session++
    state.device = null
    state.connected = false
    state.capabilities = {}
    state.files = state.files.filter((f) => f.location === 'phone')
    state.transfers.forEach((t) => {
      if (!['done', 'cancelled'].includes(t.status)) {
        t.status = 'cancelled'
        t.reason = '设备已解除配对'
      }
    })
    emit()
  }
  async function rpc(body) {
    if (!binding) fail('请先与眼镜配对')
    const credential = binding.credential
    try {
      return await hostRPC({ ...body, credential })
    } catch (error) {
      if (error.code === -13 && binding?.credential === credential) forgetBinding()
      throw error
    }
  }
  async function refresh() {
    const generation = session
    const result = await rpc({ op: 'status' })
    if (!enabled || session !== generation) return
    if (!binding || result.device_id !== binding.deviceId) {
      forgetBinding()
      fail('连接的设备已变化，请重新配对')
    }
    const lists = await Promise.all(
      ['inbox', 'notes', 'media'].map(async (folder) => {
        const { files } = await rpc({ op: 'list', folder })
        return files.map((f) => ({
          id: `remote:${folder}/${f.name}`,
          remotePath: `${folder}/${f.name}`,
          name: f.name,
          size: f.size,
          folder,
          location: 'glasses',
          kind: kindOf(f.name),
          date: '设备文件',
        }))
      }),
    )
    if (!enabled || session !== generation) return
    state.device = {
      id: result.device_id,
      name: result.runtime === 'linux-host' ? 'Linux 主机 LVGL' : 'V821 AI Glasses',
      ssid: '已配对连接',
      battery: result.battery,
      firmware: `LVGL · 协议 v${result.version}`,
    }
    state.capabilities = result.capabilities
    state.connected = true
    state.files = [...lists.flat(), ...state.files.filter((f) => f.location === 'phone')]
    emit()
  }
  async function contents(f, limit = MAX_FILE_BYTES) {
    if (f.blobId) {
      const value = await blobAction('get', f.blobId)
      if (!value) fail('本地文件已被清理，请重新导入')
      return value
    }
    connected()
    if (!f.remotePath || f.size > MAX_FILE_BYTES) fail('文件超过大小限制')
    const parts = []
    let offset = 0
    do {
      const response = await rpc({ op: 'get', path: f.remotePath, offset })
      const part = unhex(response.data_hex)
      if (response.offset !== offset + part.length || (!part.length && !response.final))
        fail('读取偏移无效')
      parts.push(part)
      offset = response.offset
      if (offset > MAX_FILE_BYTES) fail('文件超过大小限制')
      if (response.final || offset >= limit) break
    } while (true)
    const type =
      f.kind === 'photo'
        ? {
            svg: 'image/svg+xml',
            png: 'image/png',
            jpg: 'image/jpeg',
            jpeg: 'image/jpeg',
            webp: 'image/webp',
          }[f.name.split('.').pop().toLowerCase()] || ''
        : 'application/octet-stream'
    return new Blob(parts, { type })
  }
  async function cleanup(task) {
    if (task.destination === 'glasses') await rpc({ op: 'abort_put', name: task.name })
    else {
      for (let offset = 0; offset < task.offset || (offset === 0 && !task.size); offset += 4096)
        await blobAction('delete', `${task.id}:${offset}`)
    }
  }
  async function run(task) {
    working = true
    workingTask = task.id
    task.status = 'running'
    task.reason = ''
    emit()
    try {
      const source = task.source
      const blob = task.destination === 'glasses' ? await contents(source) : null
      while (task.status === 'running' && state.connected && enabled) {
        const offset = task.offset
        let response
        if (blob) {
          const part = new Uint8Array(await blob.slice(offset, offset + 4096).arrayBuffer())
          response = await rpc({
            op: 'put',
            name: task.name,
            offset,
            data_hex: hex(part),
            final: offset + part.length >= blob.size,
          })
          if (
            response.offset !== offset + part.length ||
            response.final !== (response.offset === blob.size)
          )
            fail('上传响应偏移无效')
        } else {
          response = await rpc({ op: 'get', path: source.remotePath, offset })
          const part = unhex(response.data_hex)
          if (
            response.offset !== offset + part.length ||
            (!part.length && !response.final) ||
            response.offset > task.size
          )
            fail('下载期间源文件已变化，请重新创建任务')
          await blobAction('put', `${task.id}:${offset}`, new Blob([part]))
        }
        task.offset = response.offset
        task.progress = task.size ? Math.min(99, Math.floor((task.offset * 100) / task.size)) : 0
        emit()
        if (response.final) {
          if (task.offset !== task.size) fail('文件大小已变化，传输未完成')
          if (!blob) {
            const parts = []
            for (let n = 0; n < task.offset || (n === 0 && !task.size); n += 4096) {
              const part = await blobAction('get', `${task.id}:${n}`)
              if (!part) fail('下载块已被清理，请重新传输')
              parts.push(part)
            }
            const blobId = `host-file:${task.id}`
            await blobAction(
              'put',
              blobId,
              new Blob(parts, {
                type:
                  source.kind === 'photo'
                    ? {
                        svg: 'image/svg+xml',
                        png: 'image/png',
                        jpg: 'image/jpeg',
                        jpeg: 'image/jpeg',
                        webp: 'image/webp',
                      }[source.name.split('.').pop().toLowerCase()] || ''
                    : 'application/octet-stream',
              }),
            )
            state.files.unshift({
              ...source,
              id: id(),
              location: 'phone',
              remotePath: undefined,
              blobId,
              date: date(),
            })
            await cleanup(task)
          }
          task.reason = task.status === 'cancelled' ? '取消请求到达前文件已完成' : ''
          task.status = 'done'
          task.progress = 100
          emit()
          await refresh()
          break
        }
      }
      if (task.status === 'running' && (!state.connected || !enabled)) {
        task.status = 'paused'
        task.reason = '主机连接已断开'
        emit()
      }
      if (task.status === 'cancelled') await cleanup(task)
    } catch (error) {
      if (task.status !== 'cancelled' && task.status !== 'done') {
        task.status = 'failed'
        task.reason = error.message
      }
      emit()
    } finally {
      working = false
      workingTask = null
    }
  }
  const api = {
    snapshot: () => clone(state),
    subscribe(fn) {
      listeners.add(fn)
      return () => listeners.delete(fn)
    },
    hasBinding: () => !!binding,
    async discover() {
      const result = await hostRPC({ op: 'hello' })
      if (result.version !== 2 || !/^[a-f0-9]{32}$/.test(result.device_id))
        fail('设备协议不兼容，请更新 LVGL 服务')
      return {
        id: result.device_id,
        name: result.runtime === 'linux-host' ? 'Linux 主机 LVGL' : 'V821 AI Glasses',
        paired: result.paired,
      }
    },
    async pair({ deviceId, code }) {
      if (!/^\d{6}$/.test(code)) fail('请输入屏幕上的 6 位配对码')
      const found = await api.discover()
      if (found.id !== deviceId) fail('设备已变化，请重新查找')
      const result = await hostRPC({ op: 'pair', code })
      if (!/^[a-f0-9]{64}$/.test(result.credential)) fail('配对响应无效')
      binding = { deviceId, credential: result.credential }
      emit()
      await api.connect(true)
    },
    async connect(value) {
      if (value && !binding) fail('请先与眼镜配对')
      session++
      enabled = !!value
      if (!value) {
        state.connected = false
        state.transfers.forEach((t) => {
          if (['running', 'queued'].includes(t.status)) {
            t.status = 'paused'
            t.reason = '主机连接已断开'
          }
        })
        emit()
        return
      }
      try {
        await refresh()
      } catch (error) {
        state.connected = false
        emit()
        throw error
      }
    },
    async tick() {
      if (!enabled) return
      if (!polling && Date.now() - lastPoll > 3000) {
        polling = true
        lastPoll = Date.now()
        try {
          await refresh()
        } catch {
          state.connected = false
          emit()
        } finally {
          polling = false
        }
      }
      if (!working && state.connected) {
        const task = [...state.transfers].reverse().find((t) => t.status === 'queued')
        if (task) await run(task)
      }
    },
    refresh,
    contents,
    async unpair() {
      connected()
      if (working) fail('请先暂停传输，等待当前文件块结束')
      await rpc({ op: 'unpair' })
      forgetBinding()
    },
    settings(patch) {
      Object.assign(state.settings, patch)
      emit()
    },
    importFile({ name: filename, size, blobId }) {
      filename = name(filename)
      if (!Number.isFinite(size) || size < 0 || size > MAX_FILE_BYTES)
        fail('单个文件最大支持 32 MiB')
      if (state.files.some((f) => f.location === 'phone' && f.name === filename))
        fail('手机目录已有同名文件')
      const record = {
        id: id(),
        name: filename,
        size,
        blobId,
        location: 'phone',
        folder: 'inbox',
        kind: kindOf(filename),
        date: date(),
      }
      state.files.unshift(record)
      emit()
      return clone(record)
    },
    async renameFile(fileId, value) {
      const f = file(fileId)
      const newName = name(value)
      if (busy(fileId)) fail('请先完成或取消此文件的传输')
      if (f.remotePath) {
        connected()
        await rpc({ op: 'rename', path: f.remotePath, name: newName })
        await refresh()
      } else {
        if (
          state.files.some(
            (other) => other.id !== f.id && other.location === 'phone' && other.name === newName,
          )
        )
          fail('手机目录已有同名文件')
        f.name = newName
        f.kind = kindOf(newName)
        emit()
      }
      return clone(
        state.files.find(
          (item) =>
            item.name === newName && item.location === f.location && item.folder === f.folder,
        ),
      )
    },
    async deleteFiles(ids) {
      const records = ids.map(file)
      if (ids.some(busy)) fail('请先完成或取消选中文件的传输')
      for (const f of records) {
        if (f.remotePath) {
          connected()
          await rpc({ op: 'delete', path: f.remotePath })
        }
        state.files = state.files.filter((other) => other.id !== f.id)
        emit()
      }
      if (enabled && state.connected) await refresh()
    },
    transfer(fileId) {
      connected()
      const f = file(fileId)
      if (f.size > MAX_FILE_BYTES) fail('文件超过 32 MiB')
      if (busy(fileId)) fail('此文件已有传输任务')
      const destination = f.location === 'phone' ? 'glasses' : 'phone'
      if (
        state.files.some(
          (other) =>
            other.location === destination &&
            other.name === f.name &&
            (destination === 'phone' || other.folder === 'inbox'),
        )
      )
        fail('目标位置已有同名文件')
      const task = {
        id: id(),
        fileId,
        source: clone(f),
        name: f.name,
        size: f.size,
        destination,
        offset: 0,
        progress: 0,
        status: 'queued',
        reason: '',
        date: date(),
      }
      state.transfers.unshift(task)
      emit()
      return clone(task)
    },
    async transferAction(taskId, action) {
      const t = state.transfers.find((item) => item.id === taskId) || fail('任务不存在')
      if (action === 'pause' && ['running', 'queued'].includes(t.status)) {
        t.status = 'paused'
        t.reason = '已暂停'
      } else if (action === 'resume' && ['paused', 'failed'].includes(t.status)) {
        connected()
        if (workingTask === t.id) fail('当前文件块正在结束，请稍后继续')
        if (t.status === 'failed') {
          await cleanup(t)
          t.offset = 0
          t.progress = 0
        }
        t.status = 'queued'
        t.reason = ''
      } else if (action === 'cancel' && !['done', 'cancelled'].includes(t.status)) {
        const wasRunning = workingTask === t.id
        t.status = 'cancelled'
        if (!wasRunning) await cleanup(t)
      } else fail('当前任务状态不支持此操作')
      emit()
    },
    async send(kind, text) {
      connected()
      if (!text.trim() || bytes(text) > 2047) fail('内容须为 1–2047 字节')
      let request
      if (kind === 'note') request = { op: 'note', name: `phone-${id()}.txt`, text }
      else if (kind === 'notify') request = { op: 'notify', text }
      else if (['chat', 'translate'].includes(kind)) request = { op: 'input', target: kind, text }
      else if (kind === 'navigate') {
        if (bytes(text) > 255) fail('导航文字最多 255 字节')
        request = { op: 'navigation', text, speed_kmh: 0, distance_km: 0 }
      } else fail('内容类型不支持')
      await rpc(request)
      state.messages.unshift({ id: id(), kind, text, date: date() })
      state.messages = state.messages.slice(0, 30)
      emit()
      if (kind === 'note') await refresh()
    },
    saveApp(appId) {
      state.savedApps = state.savedApps.includes(appId)
        ? state.savedApps.filter((v) => v !== appId)
        : [...state.savedApps, appId]
      emit()
    },
    clearHistory() {
      state.transfers = state.transfers.filter((t) => !['done', 'cancelled'].includes(t.status))
      emit()
    },
  }
  return api
}
