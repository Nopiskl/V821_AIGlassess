<script setup>
import { computed, ref, watch } from 'vue'
import { onLoad, onUnload } from '@dcloudio/uni-app'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import FileThumb from '../../components/FileThumb.vue'
import Sheet from '../../components/Sheet.vue'
import {
  state,
  device,
  go,
  usePairedPage,
  act,
  toast,
  confirm,
  formatSize,
  kindLabel,
} from '../../services/device.js'
import { MAX_FILE_BYTES } from '../../services/limits.js'
import { blobAction, exportFile, exportBlob } from '../../services/blob-store.js'
usePairedPage()
const location = ref('glasses'),
  kind = ref('all'),
  folder = ref('all'),
  search = ref(''),
  managing = ref(false),
  ids = ref([])
const selected = ref(null),
  rename = ref(false),
  newName = ref(''),
  preview = ref(''),
  previewURL = ref(''),
  importing = ref(false)
const categories = [
  { id: 'all', name: '全部' },
  { id: 'photo', name: '照片' },
  { id: 'video', name: '录像' },
  { id: 'audio', name: '录音' },
  { id: 'note', name: '文档' },
]
const folders = ['所有目录', '媒体', '笔记', '收件箱'],
  folderKeys = ['all', 'media', 'notes', 'inbox']
const list = computed(() =>
  state.files.filter(
    (f) =>
      f.location === location.value &&
      (kind.value === 'all' || f.kind === kind.value) &&
      (folder.value === 'all' || f.folder === folder.value) &&
      f.name.toLowerCase().includes(search.value.trim().toLowerCase()),
  ),
)
const total = computed(() => list.value.reduce((n, f) => n + f.size, 0))
watch([location, kind, folder, search], () => {
  ids.value = []
})
function toggleManaging() {
  managing.value = !managing.value
  ids.value = []
}
function startRename() {
  rename.value = true
  newName.value = selected.value.name
}
function clearPreview() {
  if (previewURL.value) URL.revokeObjectURL(previewURL.value)
  previewURL.value = ''
  preview.value = ''
}
async function openFile(file) {
  if (managing.value) {
    ids.value = ids.value.includes(file.id)
      ? ids.value.filter((id) => id !== file.id)
      : [...ids.value, file.id]
    return
  }
  clearPreview()
  selected.value = file
  rename.value = false
  preview.value = file.text || ''
  if (file.blobId || file.remotePath)
    await act(async () => {
      const blob = file.remotePath
        ? await device.contents(file, file.kind === 'photo' ? MAX_FILE_BYTES : 8192)
        : await blobAction('get', file.blobId)
      if (selected.value?.id !== file.id) return
      if (!blob) throw new Error('文件内容已被清理，请重新导入')
      if (file.kind === 'photo') previewURL.value = URL.createObjectURL(blob)
      else if (/\.(txt|md|json|csv)$/i.test(file.name))
        preview.value = (await blob.slice(0, 8192).text()).slice(0, 8192)
    })
}
async function exportSelected(file) {
  if (file.remotePath) exportBlob(await device.contents(file), file.name)
  else await exportFile(file)
}
function closeFile() {
  selected.value = null
  clearPreview()
}
async function transfer(file) {
  const task = await act(() => device.transfer(file.id), '已加入传输队列')
  if (task) {
    closeFile()
    go('transfers')
  }
}
async function saveName() {
  try {
    const updated = await device.renameFile(selected.value.id, newName.value)
    selected.value = updated || state.files.find((f) => f.id === selected.value.id)
    rename.value = false
    toast('名称已更新')
  } catch (e) {
    toast(e.message)
  }
}
function remove(targets) {
  confirm(
    '删除文件',
    `将从${location.value === 'glasses' ? '设备目录' : '手机本地目录'}中删除 ${targets.length} 个文件。`,
    async () => {
      const removed = state.files.filter((f) => targets.includes(f.id))
      await device.deleteFiles(targets)
      ids.value = []
      closeFile()
      for (const file of removed)
        if (file.blobId && !state.files.some((f) => f.blobId === file.blobId))
          await blobAction('delete', file.blobId)
      toast('文件已删除')
    },
  )
}
function chooseFile() {
  if (importing.value) return
  uni.chooseFile({
    count: 1,
    type: 'all',
    success: async (result) => {
      importing.value = true
      let key
      await act(async () => {
        const item = result.tempFiles[0]
        if (item.size > MAX_FILE_BYTES) throw new Error('单个文件最大支持 32 MiB')
        const blob = item.file instanceof Blob ? item.file : await (await fetch(item.path)).blob()
        key = `import-${Date.now()}-${Math.random().toString(36).slice(2)}`
        await blobAction('put', key, blob)
        try {
          await device.importFile({ name: item.name, size: blob.size, blobId: key })
        } catch (e) {
          await blobAction('delete', key)
          throw e
        }
        location.value = 'phone'
        kind.value = folder.value = 'all'
        search.value = ''
        toast('文件已导入手机目录')
      })
      importing.value = false
    },
    fail: (result) => {
      if (!result.errMsg?.includes('cancel')) toast('无法选择文件，请使用支持文件选择的浏览器')
    },
  })
}
onLoad((options) => {
  if (options.location === 'phone') location.value = 'phone'
  if (options.id) {
    const file = state.files.find((f) => f.id === options.id)
    if (file) {
      location.value = file.location
      openFile(file)
    }
  }
})
onUnload(clearPreview)
</script>
<template>
  <AppShell title="文件管理" subtitle="让每一份记录，都各得其所。" previous>
    <template #action>
      <button class="text-button" @click="toggleManaging">
        {{ managing ? '完成' : '管理' }}
      </button>
    </template>
    <view class="segmented">
      <button :class="{ active: location === 'glasses' }" @click="location = 'glasses'">
        眼镜文件
      </button>
      <button :class="{ active: location === 'phone' }" @click="location = 'phone'">
        手机文件
      </button>
    </view>
    <view class="search-field" style="margin-top: 16px">
      <AppIcon name="search" :size="18" color="#9aa5b8" />
      <input v-model="search" placeholder="搜索文件名" aria-label="搜索文件" />
    </view>
    <view class="chips file-categories">
      <button
        v-for="item in categories"
        :key="item.id"
        class="chip"
        :class="{ active: kind === item.id }"
        @click="kind = item.id"
      >
        {{ item.name }}
      </button>
    </view>
    <view class="row between file-toolbar">
      <picker
        :range="folders"
        :value="folderKeys.indexOf(folder)"
        @change="folder = folderKeys[$event.detail.value]"
      >
        <view class="row" style="gap: 4px">
          <text class="small">{{ folders[folderKeys.indexOf(folder)] }}</text>
          <AppIcon name="chevron" :size="12" color="#8a94a7" />
        </view>
      </picker>
      <text class="small muted">{{ list.length }} 项 · {{ formatSize(total) }}</text>
    </view>
    <view v-if="list.length" class="card files-list">
      <button v-for="file in list" :key="file.id" class="list-row" @click="openFile(file)">
        <view v-if="managing" class="file-select" :class="{ checked: ids.includes(file.id) }">
          <AppIcon v-if="ids.includes(file.id)" name="check" color="#fff" :size="12" />
        </view>
        <FileThumb :file="file" />
        <view class="grow">
          <text class="row-title truncate">{{ file.name }}</text>
          <text class="row-subtitle">{{ kindLabel(file.kind) }} · {{ formatSize(file.size) }}</text>
          <text class="file-date">{{ file.date }}</text>
        </view>
        <AppIcon v-if="!managing" name="chevron" color="#a1abc0" :size="15" />
      </button>
    </view>
    <view v-else class="empty">
      <view class="empty-icon"><AppIcon name="folder" color="#8b9cbd" :size="30" /></view>
      <text class="row-title">这里还没有文件</text>
      <text class="row-subtitle">可以调整筛选条件，或导入一份手机文件。</text>
    </view>
    <view v-if="managing" class="button-row">
      <button
        class="secondary"
        @click="ids = ids.length === list.length ? [] : list.map((f) => f.id)"
      >
        {{ ids.length === list.length && ids.length ? '取消全选' : '全选' }}
      </button>
      <button class="danger-button" :disabled="!ids.length" @click="remove(ids)">
        删除 {{ ids.length ? `(${ids.length})` : '' }}
      </button>
    </view>
    <button
      v-else
      class="secondary wide"
      style="margin-top: 22px"
      :disabled="importing"
      @click="chooseFile"
    >
      <AppIcon name="plus" :size="18" color="#667eea" />
      {{ importing ? '正在导入…' : '导入手机文件' }}
    </button>
    <text class="file-footer">
      设备文件来自 LVGL 工作目录；手机文件保存在浏览器。单文件上限 32 MiB。
    </text>
    <Sheet :open="!!selected" title="文件详情" @close="closeFile">
      <template v-if="selected">
        <image v-if="previewURL" :src="previewURL" class="preview-photo" mode="aspectFit" />
        <FileThumb v-else :file="selected" large />
        <text class="detail-name">{{ selected.name }}</text>
        <text class="row-subtitle">
          {{ kindLabel(selected.kind) }} · {{ formatSize(selected.size) }} · {{ selected.date }}
        </text>
        <view v-if="preview" class="text-preview">{{ preview }}</view>
        <template v-if="rename">
          <text class="field-label">新文件名</text>
          <view class="field">
            <input
              v-model="newName"
              @blur="newName = $event.detail.value"
              maxlength="120"
              placeholder="新文件名"
              aria-label="新文件名"
            />
          </view>
          <view class="button-row">
            <button class="secondary" @click="rename = false">取消</button>
            <button class="primary" @click="saveName">保存名称</button>
          </view>
        </template>
        <template v-else>
          <button class="primary wide" style="margin-top: 22px" @click="transfer(selected)">
            <AppIcon name="transfer" color="#fff" :size="19" />
            {{ selected.location === 'glasses' ? '传到手机' : '传到眼镜' }}
          </button>
          <view class="detail-actions">
            <button @click="startRename">
              <AppIcon name="edit" :size="19" color="#8a94a7" />
              重命名
            </button>
            <button @click="act(() => exportSelected(selected))">
              <AppIcon name="download" :size="19" color="#8a94a7" />
              导出文件
            </button>
            <button @click="remove([selected.id])">
              <AppIcon name="trash" :size="19" color="#c57272" />
              删除
            </button>
          </view>
        </template>
      </template>
    </Sheet>
  </AppShell>
</template>
<style scoped>
.file-categories {
  gap: 6px;
}
.file-categories .chip {
  padding: 7px 12px;
}
.file-toolbar {
  margin: 22px 2px 12px;
}
.files-list {
  padding: 0 15px;
}
.files-list .row-title {
  font-size: 12px;
}
.file-date {
  display: block;
  font-size: 9px;
  color: #a4aec0;
  margin-top: 4px;
}
.file-select {
  width: 17px;
  height: 17px;
  border-radius: 5px;
  border: 1px solid #bac5d8;
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}
.file-select.checked {
  background: #667eea;
  border-color: #667eea;
}
.file-footer {
  font-size: 10px;
  color: var(--muted);
  text-align: center;
  display: block;
  margin-top: 18px;
}
.detail-name {
  font-size: 18px;
  font-weight: 600;
  word-break: break-all;
  display: block;
}
.text-preview {
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 180px;
  overflow: auto;
  font-size: 12px;
  color: var(--muted);
  line-height: 1.8;
  padding: 15px;
  background: var(--input);
  border-radius: 13px;
  margin-top: 18px;
}
.detail-actions {
  display: flex;
  margin-top: 20px;
}
.detail-actions button {
  flex: 1;
  flex-direction: column;
  gap: 6px;
  font-size: 11px;
  color: var(--muted);
  min-height: 50px;
}
.preview-photo {
  width: 100%;
  height: 200px;
  margin: 16px 0;
  border-radius: 16px;
}
</style>
