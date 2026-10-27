<script setup>
import { computed } from 'vue'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import { state, device, usePairedPage, confirm, toast, formatSize } from '../../services/device.js'
usePairedPage()
const used = computed(() =>
  state.files.filter((f) => f.location === 'glasses').reduce((sum, file) => sum + file.size, 0),
)
async function connect() {
  if (state.connected)
    confirm('断开设备连接', '将暂停未完成的传输，保留配对关系与文件。', () => device.connect(false))
  else {
    try {
      await device.connect(true)
      toast('设备已重新连接')
    } catch (error) {
      toast(error.message)
    }
  }
}
</script>
<template>
  <AppShell title="我的眼镜" subtitle="设备状态与连接管理。" previous>
    <view class="card device-card">
      <view class="row">
        <text class="pill" :class="{ green: state.connected }">
          {{ state.connected ? '已连接' : '未连接' }}
        </text>
      </view>
      <image src="/static/glasses-product.png" mode="aspectFit" />
      <text class="device-name">{{ state.device?.name }}</text>
      <text class="device-model">设备标识 {{ state.device?.id }}</text>
    </view>
    <view class="card device-details">
      <view class="list-row">
        <AppIcon name="battery" color="#8c9abc" />
        <text class="grow small">剩余电量</text>
        <text>{{ state.device?.battery >= 0 ? state.device.battery + '%' : '不适用' }}</text>
      </view>
      <view class="list-row">
        <AppIcon name="wifi" color="#8c9abc" />
        <text class="grow small">连接状态</text>
        <text class="small">{{ state.connected ? '已配对连接' : '配对已保存' }}</text>
      </view>
      <view class="list-row">
        <AppIcon name="folder" color="#8c9abc" />
        <text class="grow small">文件占用</text>
        <text class="small">{{ formatSize(used) }}</text>
      </view>
      <view class="list-row">
        <AppIcon name="info" color="#8c9abc" />
        <text class="grow small">软件信息</text>
        <text class="small muted">{{ state.device?.firmware }}</text>
      </view>
    </view>
    <button class="primary wide" style="margin-top: 20px" @click="connect">
      {{ state.connected ? '断开连接' : '重新连接' }}
    </button>
    <button
      class="text-button wide"
      style="margin-top: 22px; color: #bc7070"
      @click="
        confirm(
          '解除绑定',
          '将解除眼镜端配对并取消未完成的任务，文件保留。重新连接需要输入新的配对码。',
          () => device.unpair(),
        )
      "
    >
      解除设备绑定
    </button>
  </AppShell>
</template>
<style scoped>
.device-card {
  text-align: center;
}
.device-card image {
  width: 100%;
  height: 200px;
}
.device-name {
  font-size: 23px;
  font-weight: 650;
  display: block;
  word-break: break-all;
}
.device-model {
  display: block;
  font-size: 10px;
  color: var(--muted);
  margin-top: 9px;
  overflow-wrap: anywhere;
}
.device-details {
  padding: 0 18px;
  margin-top: 20px;
}
</style>
