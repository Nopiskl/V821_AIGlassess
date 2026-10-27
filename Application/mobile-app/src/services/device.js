import { reactive } from 'vue'
import { onShow } from '@dcloudio/uni-app'
import { createHostDevice } from './host-device.js'

const storage = {
  getItem: (key) => uni.getStorageSync(key),
  setItem: (key, value) => uni.setStorageSync(key, value),
}
export const device = createHostDevice({ storage })
export const state = reactive(device.snapshot())
device.subscribe((next) => {
  const lostBinding = state.device && !next.device
  const completed = next.transfers.find(
    (t) =>
      t.status === 'done' &&
      state.transfers.some((previous) => previous.id === t.id && previous.status !== 'done'),
  )
  Object.assign(state, next)
  if (completed && next.settings.notifications) toast(`传输完成：${completed.name}`)
  if (lostBinding) tab('setup')
})
let timer
export function startDevice() {
  if (!timer) timer = setInterval(() => device.tick().catch(() => {}), 350)
  if (device.hasBinding()) device.connect(true).catch(() => {})
}
export function usePairedPage() {
  onShow(() => {
    if (!device.hasBinding()) tab('setup')
  })
}
export const go = (page, query = '') =>
  uni.navigateTo({ url: `/pages/${page}/index${query ? '?' + query : ''}` })
let switchingTo = ''
export function tab(page) {
  const route = `pages/${page}/index`
  const currentRoute = () => getCurrentPages().at(-1)?.route
  // A revoked binding and the page guard can request the same redirect together.
  if (switchingTo === route || currentRoute() === route) return
  switchingTo = route
  uni.reLaunch({
    url: `/${route}`,
    fail: () => {
      if (currentRoute() !== route) toast('页面切换未完成，请重试')
    },
    complete: () => {
      if (switchingTo === route) switchingTo = ''
    },
  })
}
export function back() {
  if (getCurrentPages().length > 1) uni.navigateBack()
  else tab('home')
}
export const toast = (title) => uni.showToast({ title, icon: 'none', duration: 2200 })
export async function act(action, message) {
  try {
    const result = await action()
    if (message) toast(message)
    return result
  } catch (e) {
    toast(e.message || '操作失败，请重试')
    return null
  }
}
export function confirm(title, content, action) {
  uni.showModal({
    title,
    content,
    confirmText: '确定',
    cancelText: '取消',
    confirmColor: '#315ce8',
    success: (result) => {
      if (result.confirm) act(action)
    },
  })
}
export function formatSize(bytes) {
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB'
  if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB'
  return bytes + ' B'
}
export const kindLabel = (kind) =>
  ({ photo: '照片', video: '录像', audio: '录音', note: '文档' })[kind] || '文件'
