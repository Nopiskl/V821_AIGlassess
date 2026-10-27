<script setup>
import { computed } from 'vue'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import FileThumb from '../../components/FileThumb.vue'
import { state, go, tab, usePairedPage, formatSize } from '../../services/device.js'
usePairedPage()
const files = computed(() => state.files.filter((f) => f.location === 'glasses'))
const transfers = computed(
  () => state.transfers.filter((t) => ['running', 'queued', 'paused'].includes(t.status)).length,
)
const used = computed(() => files.value.reduce((total, f) => total + f.size, 0))
</script>
<template>
  <AppShell
    title="你的视界，即刻相连。"
    :eyebrow="`你好，${state.settings.nickname}`"
    active="home"
  >
    <template #action>
      <button class="profile-mark" aria-label="个人设置" @click="tab('me')">
        {{ state.settings.nickname.slice(0, 1) }}
      </button>
    </template>
    <button class="device-hero" aria-label="查看眼镜设备" @click="go('device')">
      <view class="hero-heading">
        <view>
          <text class="hero-kicker">MY EVERYDAY COMPANION</text>
          <text class="hero-name">{{ state.device?.name || 'V821 AI Glasses' }}</text>
        </view>
        <view class="connection">
          <view :class="['connection-dot', { offline: !state.connected }]" />
          {{ state.connected ? '已连接' : '未连接' }}
        </view>
      </view>
      <text class="hero-watermark">V821</text>
      <image class="hero-glasses" src="/static/glasses-product.png" mode="aspectFit" />
      <view class="hero-bottom">
        <view class="row">
          <AppIcon name="battery" color="#c8d5f7" :size="19" />
          <text class="hero-battery">
            {{ state.device?.battery >= 0 ? state.device.battery : '--' }}
            <text class="percent">{{ state.device?.battery >= 0 ? '%' : '' }}</text>
          </text>
          <view class="hero-divider" />
          <AppIcon name="wifi" color="#bac9ed" :size="16" />
          <text class="hero-network">{{ state.device?.ssid || '等待连接' }}</text>
        </view>
        <view class="hero-arrow"><AppIcon name="arrow" color="#e5ebff" :size="17" /></view>
      </view>
    </button>
    <view class="overview">
      <view>
        <text class="overview-value mono">
          {{ files.length.toString().padStart(2, '0') }}
          <text>份</text>
        </text>
        <text class="overview-label">眼镜里的记录</text>
      </view>
      <view>
        <text class="overview-value mono">{{ formatSize(used) }}</text>
        <text class="overview-label">设备文件占用</text>
      </view>
      <button @click="go('transfers')">
        <text class="overview-value mono">
          {{ transfers.toString().padStart(2, '0') }}
          <text>项</text>
        </text>
        <text class="overview-label">待完成传输</text>
      </button>
    </view>
    <view class="quick-actions">
      <button @click="go('files')">
        <view class="quick-symbol"><AppIcon name="folder" color="#667eea" :size="23" /></view>
        <view class="grow">
          <text class="row-title">文件管理</text>
          <text class="row-subtitle">整理每一份记录</text>
        </view>
        <AppIcon name="chevron" :size="14" color="#a1abc0" />
      </button>
      <button @click="go('transfers')">
        <view class="quick-symbol warm">
          <AppIcon name="transfer" color="#a77956" :size="23" />
        </view>
        <view class="grow">
          <text class="row-title">文件传输</text>
          <text class="row-subtitle">手机与眼镜之间</text>
        </view>
        <AppIcon name="chevron" :size="14" color="#a1abc0" />
      </button>
    </view>
    <view class="section-heading">
      <view>
        <text class="section-title">最近记录</text>
        <text class="section-caption">生活里的小片段，值得留住。</text>
      </view>
      <button class="text-button" @click="go('files')">
        全部
        <AppIcon name="arrow" :size="15" color="#667eea" />
      </button>
    </view>
    <view class="recent-card card">
      <button
        v-for="file in files.slice(0, 3)"
        :key="file.id"
        class="list-row"
        @click="go('files', `id=${encodeURIComponent(file.id)}`)"
      >
        <FileThumb :file="file" />
        <view class="grow">
          <text class="row-title truncate">{{ file.name }}</text>
          <text class="row-subtitle">{{ file.date }} · {{ formatSize(file.size) }}</text>
        </view>
        <AppIcon name="chevron" :size="15" color="#a1abc0" />
      </button>
      <view v-if="!files.length" class="empty">
        <text class="muted small">暂无记录，可导入文件或同步笔记。</text>
      </view>
    </view>
    <button class="discovery-card" @click="go('compose', 'kind=note')">
      <view>
        <text class="discovery-label">一起试试 / 01</text>
        <text class="discovery-title">把文字同步到眼前</text>
        <text class="discovery-description">写一条笔记，在 LVGL 中查看。</text>
      </view>
      <view class="camera-orbit"><AppIcon name="note" :size="32" color="#7885c5" /></view>
      <AppIcon name="arrow" :size="19" color="#667eea" />
    </button>
  </AppShell>
</template>
<style scoped>
.profile-mark {
  width: 37px;
  height: 37px;
  flex-shrink: 0;
  border-radius: 13px;
  background: linear-gradient(135deg, #667eea, #8a6ab4);
  color: white;
  font-size: 14px;
}
.device-hero {
  display: block;
  position: relative;
  width: 100%;
  height: 270px;
  overflow: hidden;
  text-align: left;
  border-radius: 25px;
  color: #fff;
  background: linear-gradient(120deg, #263858, #252b48 64%, #373257);
  box-shadow: 0 12px 25px #263b5b15;
  padding: 23px;
}
.hero-heading {
  position: relative;
  z-index: 2;
  display: flex;
  justify-content: space-between;
  gap: 8px;
}
.hero-kicker {
  color: #a1afce;
  font-size: 8px;
  letter-spacing: 1.4px;
  display: block;
}
.hero-name {
  display: block;
  font-size: 20px;
  font-weight: 600;
  margin-top: 6px;
  max-width: 205px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.connection {
  display: flex;
  gap: 5px;
  align-items: center;
  align-self: flex-start;
  border: 1px solid #c6d7fa22;
  padding: 5px 8px;
  border-radius: 20px;
  font-size: 9px;
  color: #cad6ee;
}
.connection-dot {
  width: 5px;
  height: 5px;
  background: #9cdbbf;
  border-radius: 50%;
}
.connection-dot.offline {
  background: #b0b6c5;
}
.hero-watermark {
  position: absolute;
  left: 28px;
  top: 88px;
  color: #afc6ef0a;
  font-size: 104px;
  letter-spacing: -6px;
  font-weight: 800;
  line-height: 1;
}
.hero-glasses {
  position: absolute;
  width: 100%;
  height: 170px;
  top: 68px;
  left: 0;
}
.hero-bottom {
  position: absolute;
  bottom: 20px;
  left: 23px;
  right: 23px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.hero-bottom .row {
  gap: 7px;
}
.hero-battery {
  font-size: 19px;
  font-weight: 600;
}
.percent {
  font-size: 10px;
  margin-left: 2px;
  color: #adbbd5;
}
.hero-divider {
  width: 1px;
  height: 14px;
  background: #c5d8ff33;
  margin: 0 5px;
}
.hero-network {
  font-size: 10px;
  color: #b8c7e5;
  max-width: 100px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.hero-arrow {
  width: 28px;
  height: 28px;
  border-radius: 50%;
  border: 1px solid #c5d8ff33;
  display: flex;
  align-items: center;
  justify-content: center;
}
.overview {
  display: flex;
  justify-content: space-between;
  padding: 24px 8px;
}
.overview > view,
.overview > button {
  flex-direction: column;
  display: flex;
  text-align: center;
  min-width: 0;
}
.overview-value {
  font-size: 20px;
  font-weight: 650;
  letter-spacing: -0.5px;
}
.overview-value text {
  font-size: 9px;
  color: var(--muted);
  margin-left: 3px;
  font-weight: normal;
}
.overview-label {
  margin-top: 5px;
  color: var(--muted);
  font-size: 10px;
}
.quick-actions {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}
.quick-actions button {
  padding: 17px 12px;
  background: var(--surface);
  border-radius: 17px;
  text-align: left;
  gap: 8px;
}
.quick-actions .row-title {
  font-size: 12px;
}
.quick-actions .row-subtitle {
  font-size: 9px;
  margin-top: 3px;
}
.quick-symbol {
  display: flex;
  justify-content: center;
  align-items: center;
  flex-shrink: 0;
}
.quick-actions .grow {
  min-width: 0;
}
.recent-card {
  padding: 2px 16px;
}
.recent-card .list-row {
  padding: 13px 0;
}
.recent-card .row-title {
  font-size: 12px;
}
.recent-card .row-subtitle {
  font-size: 9px;
}
.discovery-card {
  position: relative;
  display: flex;
  justify-content: space-between;
  gap: 6px;
  width: 100%;
  text-align: left;
  border-radius: 19px;
  padding: 21px 19px;
  background: var(--soft);
  margin-top: 18px;
  overflow: hidden;
}
.discovery-label {
  display: block;
  font-size: 9px;
  color: #8895b8;
  letter-spacing: 1px;
}
.discovery-title {
  display: block;
  font-size: 16px;
  margin-top: 7px;
  font-weight: 600;
}
.discovery-description {
  display: block;
  color: var(--muted);
  font-size: 10px;
  margin-top: 6px;
}
.camera-orbit {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 64px;
  height: 64px;
  border: 1px solid #899fd530;
  border-radius: 50%;
  transform: rotate(-13deg);
}
@media (max-width: 350px) {
  .hero-name {
    max-width: 160px;
  }
  .hero-network {
    max-width: 70px;
  }
  .quick-actions button {
    padding-left: 10px;
    padding-right: 10px;
  }
  .quick-actions button > .app-icon {
    display: none;
  }
}
</style>
