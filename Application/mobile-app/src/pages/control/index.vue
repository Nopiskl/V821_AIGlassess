<script setup>
import { ref } from 'vue'
import { onLoad } from '@dcloudio/uni-app'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import { go, usePairedPage } from '../../services/device.js'
usePairedPage()
const mode = ref('photo')
const modes = [
  { id: 'photo', name: '拍照', icon: 'camera' },
  { id: 'video', name: '录像', icon: 'video' },
  { id: 'audio', name: '录音', icon: 'mic' },
]
onLoad((query) => {
  if (modes.some((item) => item.id === query.mode)) mode.value = query.mode
})
</script>
<template>
  <AppShell title="眼镜遥控" subtitle="相机与麦克风功能。" previous>
    <view class="notice error">主机联调未接入相机和麦克风。拍照、录像、录音暂不可用。</view>
    <view class="control-preview">
      <AppIcon :name="modes.find((item) => item.id === mode).icon" :size="48" color="#9daaca" />
      <text>需要设备端媒体接口</text>
    </view>
    <view class="mode-tabs">
      <button
        v-for="item in modes"
        :key="item.id"
        :class="{ active: mode === item.id }"
        @click="mode = item.id"
      >
        {{ item.name }}
      </button>
    </view>
    <view class="shutter-row">
      <button class="shutter-side" @click="go('files')">
        <AppIcon name="folder" color="#8a94a7" :size="22" />
        <text>文件</text>
      </button>
      <button class="shutter" disabled aria-label="媒体功能不可用">
        <AppIcon :name="modes.find((item) => item.id === mode).icon" color="#fff" :size="29" />
      </button>
      <button class="shutter-side" @click="go('device')">
        <AppIcon name="settings" color="#8a94a7" :size="22" />
        <text>设备</text>
      </button>
    </view>
  </AppShell>
</template>
<style scoped>
.control-preview {
  height: 300px;
  border-radius: 24px;
  background: #232d46;
  margin: 20px 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  gap: 20px;
  color: #9daaca;
  font-size: 12px;
}
.mode-tabs {
  display: flex;
  justify-content: center;
  gap: 24px;
}
.mode-tabs button {
  color: var(--muted);
  padding: 8px;
  font-size: 14px;
}
.mode-tabs .active {
  color: #667eea;
  border-bottom: 2px solid #667eea;
}
.shutter-row {
  display: flex;
  align-items: center;
  justify-content: space-around;
  margin-top: 30px;
}
.shutter {
  width: 76px;
  height: 76px;
  border-radius: 50%;
  background: #667eea;
}
.shutter-side {
  display: flex;
  flex-direction: column;
  gap: 6px;
  font-size: 11px;
  color: var(--muted);
}
</style>
