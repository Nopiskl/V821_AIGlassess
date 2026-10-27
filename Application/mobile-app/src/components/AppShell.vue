<script setup>
import AppIcon from './AppIcon.vue'
import { state, tab, back, go } from '../services/device.js'
defineProps({ title: String, eyebrow: String, subtitle: String, active: String, previous: Boolean })
const items = [
  { id: 'home', title: '主页面', icon: 'home' },
  { id: 'features', title: '功能', icon: 'grid' },
  { id: 'store', title: '商店', icon: 'store' },
  { id: 'me', title: '我的', icon: 'user' },
]
</script>
<template>
  <view
    class="app-shell"
    :class="[
      { 'with-tabs': active, compact: state.settings.compact },
      `theme-${state.settings.theme}`,
    ]"
  >
    <view class="top-line">
      <text>V821 / COMPANION</text>
      <view class="demo-badge">
        <view v-if="state.connected" class="status-dot" />
        {{ state.device ? '主机联合调试' : '设备待配对' }}
      </view>
    </view>
    <view class="page-header">
      <view class="header-main">
        <button v-if="previous" class="icon-button back-button" aria-label="返回" @click="back">
          <AppIcon name="back" />
        </button>
        <view>
          <text v-if="eyebrow" class="eyebrow">{{ eyebrow }}</text>
          <text class="page-title">{{ title }}</text>
          <text v-if="subtitle" class="page-subtitle">{{ subtitle }}</text>
        </view>
      </view>
      <slot name="action" />
    </view>
    <view v-if="state.storageWarning" class="notice error">{{ state.storageWarning }}</view>
    <view v-if="state.device && !state.connected" class="offline-bar">
      <text>设备已断开，设备操作暂不可用</text>
      <button class="text-button" @click="go('device')">连接</button>
    </view>
    <view class="page-content"><slot /></view>
    <view v-if="active" class="tab-bar">
      <button
        v-for="item in items"
        :key="item.id"
        class="tab-item"
        :class="{ selected: active === item.id }"
        :aria-label="item.title"
        :aria-current="active === item.id ? 'page' : undefined"
        @click="active !== item.id && tab(item.id)"
      >
        <view class="tab-icon">
          <AppIcon
            :name="item.icon"
            :color="active === item.id ? '#315ce8' : '#8a94a7'"
            :size="23"
          />
        </view>
        <text>{{ item.title }}</text>
      </button>
    </view>
  </view>
</template>
