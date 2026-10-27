<script setup>
import { ref, computed } from 'vue'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import { state, device, go, usePairedPage, act, formatSize } from '../../services/device.js'
usePairedPage()
const filter = ref('active')
const active = computed(() =>
  state.transfers.filter((t) => !['done', 'cancelled'].includes(t.status)),
)
const history = computed(() =>
  state.transfers.filter((t) => ['done', 'cancelled'].includes(t.status)),
)
const list = computed(() => (filter.value === 'active' ? active.value : history.value))
const labels = {
  queued: '等待传输',
  running: '传输中',
  paused: '已暂停',
  failed: '传输失败',
  done: '已完成',
  cancelled: '已取消',
}
</script>
<template>
  <AppShell title="文件传输" subtitle="从眼镜到手机，让记录自由流动。" previous>
    <view class="transfer-link">
      <view class="link-end">
        <view class="link-symbol"><AppIcon name="glasses" color="#667eea" :size="29" /></view>
        <text>LVGL</text>
      </view>
      <view class="link-middle">
        <text>{{ state.connected ? '已配对连接' : '连接已断开' }}</text>
        <view class="link-line">
          <view />
          <AppIcon name="transfer" color="#96a6cc" :size="18" />
          <view />
        </view>
        <text class="link-speed">实际文件内容传输</text>
      </view>
      <view class="link-end">
        <view class="link-symbol phone-symbol"><view class="phone-outline" /></view>
        <text>这台手机</text>
      </view>
    </view>
    <view class="segmented">
      <button :class="{ active: filter === 'active' }" @click="filter = 'active'">
        进行中 {{ active.length ? `(${active.length})` : '' }}
      </button>
      <button :class="{ active: filter === 'history' }" @click="filter = 'history'">
        已结束 {{ history.length ? `(${history.length})` : '' }}
      </button>
    </view>
    <view v-if="!list.length" class="empty">
      <view class="empty-icon"><AppIcon name="transfer" color="#8b9cbd" :size="30" /></view>
      <text class="row-title">{{ filter === 'active' ? '传输队列很清爽' : '还没有传输记录' }}</text>
      <text class="row-subtitle">
        在文件管理中选择文件，
        <br />
        即可开始体验双向传输。
      </text>
      <button class="primary" @click="go('files')">
        选择文件
        <AppIcon name="arrow" :size="17" color="#fff" />
      </button>
    </view>
    <view v-for="task in list" :key="task.id" class="card transfer-task">
      <view class="row">
        <view class="tile-icon">
          <AppIcon :name="task.destination === 'phone' ? 'download' : 'upload'" color="#667eea" />
        </view>
        <view class="grow">
          <text class="row-title truncate">{{ task.name }}</text>
          <text class="row-subtitle">
            {{ task.destination === 'phone' ? '眼镜 → 手机' : '手机 → 眼镜' }} ·
            {{ formatSize(task.size) }}
          </text>
        </view>
      </view>
      <view class="task-status">
        <text :class="{ failure: task.status === 'failed' }">{{ labels[task.status] }}</text>
        <text class="mono">{{ task.progress }}%</text>
      </view>
      <view class="progress-track">
        <view
          class="progress-value"
          :style="{
            width: task.progress + '%',
            background: task.status === 'failed' ? '#c67d69' : '',
          }"
        />
      </view>
      <text v-if="task.reason" class="task-reason">{{ task.reason }}</text>
      <view class="task-actions">
        <button
          v-if="['queued', 'running'].includes(task.status)"
          @click="act(() => device.transferAction(task.id, 'pause'))"
        >
          <AppIcon name="pause" :size="14" color="#667eea" />
          暂停
        </button>
        <button
          v-if="['paused', 'failed'].includes(task.status)"
          :disabled="!state.connected"
          @click="act(() => device.transferAction(task.id, 'resume'))"
        >
          <AppIcon name="play" :size="14" color="#667eea" />
          {{ task.status === 'failed' ? '重试' : '继续' }}
        </button>
        <button
          v-if="!['done', 'cancelled'].includes(task.status)"
          @click="act(() => device.transferAction(task.id, 'cancel'))"
        >
          取消
        </button>
        <button v-if="task.status === 'done'" @click="go('files', `location=${task.destination}`)">
          查看文件
          <AppIcon name="arrow" :size="14" color="#667eea" />
        </button>
      </view>
    </view>
    <button v-if="list.length" class="secondary wide" style="margin-top: 22px" @click="go('files')">
      <AppIcon name="plus" :size="18" color="#667eea" />
      添加传输
    </button>
    <view class="notice" style="margin-top: 24px">
      文件只有在传输完成后才会出现在目标目录。刷新页面会暂停未完成的任务，可手动继续。
    </view>
  </AppShell>
</template>
<style scoped>
.transfer-link {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin: 12px 0 32px;
}
.link-end {
  flex-shrink: 0;
  text-align: center;
  font-size: 10px;
  color: var(--muted);
}
.link-symbol {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 62px;
  height: 62px;
  border-radius: 21px;
  background: var(--surface);
  margin-bottom: 10px;
}
.phone-outline {
  width: 18px;
  height: 31px;
  border: 1.8px solid #8c82ad;
  border-radius: 5px;
  position: relative;
}
.phone-outline::after {
  content: '';
  position: absolute;
  bottom: 3px;
  left: 5px;
  width: 5px;
  border-bottom: 1px solid #8c82ad;
}
.link-middle {
  text-align: center;
  flex: 1;
  padding: 0 12px;
  font-size: 10px;
  color: #8b9bc0;
}
.link-line {
  display: flex;
  align-items: center;
  gap: 7px;
  margin: 10px 0;
}
.link-line > view {
  flex: 1;
  border-top: 1px dashed #bac8e4;
}
.link-speed {
  font-size: 8px;
  color: var(--muted);
}
.transfer-task {
  margin-top: 17px;
  padding: 18px;
}
.transfer-task .row-title {
  font-size: 12px;
}
.task-status {
  display: flex;
  justify-content: space-between;
  margin: 20px 0 9px;
  color: #7b8fb9;
  font-size: 10px;
}
.task-status .failure {
  color: #bf6b56;
}
.task-reason {
  color: var(--muted);
  font-size: 10px;
  display: block;
  margin-top: 10px;
}
.task-actions {
  display: flex;
  justify-content: flex-end;
  gap: 20px;
  margin-top: 14px;
}
.task-actions button {
  gap: 5px;
  color: #667eea;
  font-size: 11px;
  min-height: 28px;
}
</style>
