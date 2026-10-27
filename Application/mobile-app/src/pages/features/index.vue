<script setup>
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import { state, go, usePairedPage } from '../../services/device.js'
usePairedPage()
const groups = [
  {
    label: '记录当下',
    number: '01',
    items: [
      {
        name: '相机遥控',
        description: '拍照与录像，掌握在手中',
        icon: 'camera',
        page: 'control',
        query: 'mode=photo',
      },
      {
        name: '录音机',
        description: '开始、停止与录音记录',
        icon: 'mic',
        page: 'control',
        query: 'mode=audio',
      },
    ],
  },
  {
    label: '连接视线',
    number: '02',
    items: [
      {
        name: '笔记同步',
        description: '把文字放进眼镜的笔记本',
        icon: 'note',
        page: 'compose',
        query: 'kind=note',
      },
      {
        name: '消息投送',
        description: '向眼镜发送一条简短提醒',
        icon: 'message',
        page: 'compose',
        query: 'kind=notify',
      },
      {
        name: 'AI 对话草稿',
        description: '从手机编辑对话输入',
        icon: 'spark',
        page: 'compose',
        query: 'kind=chat',
      },
      {
        name: '翻译草稿',
        description: '编辑要发送到翻译页的文字',
        icon: 'translate',
        page: 'compose',
        query: 'kind=translate',
      },
      {
        name: '导航卡片',
        description: '预览路线文字的投送流程',
        icon: 'compass',
        page: 'compose',
        query: 'kind=navigate',
      },
    ],
  },
]
</script>
<template>
  <AppShell title="功能" subtitle="让每一项能力，触手可及。" active="features">
    <template #action>
      <button class="icon-button" aria-label="设备管理" @click="go('device')">
        <AppIcon name="settings" />
      </button>
    </template>
    <view class="feature-connection">
      <AppIcon name="glasses" color="#667eea" :size="25" />
      <text class="grow truncate">{{ state.device?.name }}</text>
      <text class="pill" :class="{ green: state.connected }">
        {{ state.connected ? '已连接' : '离线' }}
      </text>
    </view>
    <view class="feature-primary">
      <button class="file-feature" @click="go('files')">
        <view class="feature-art">
          <AppIcon name="folder" color="#667eea" :size="32" />
          <view class="art-line" />
        </view>
        <text class="feature-title">文件管理</text>
        <text class="feature-desc">照片、录音与笔记</text>
        <view class="feature-bottom">
          <text>管理文件</text>
          <AppIcon name="arrow" color="#667eea" :size="17" />
        </view>
      </button>
      <button class="transfer-feature" @click="go('transfers')">
        <view class="feature-art">
          <AppIcon name="transfer" color="#987758" :size="32" />
          <view class="art-line" />
        </view>
        <text class="feature-title">文件传输</text>
        <text class="feature-desc">双向传送，随时查看</text>
        <view class="feature-bottom">
          <text>查看任务</text>
          <AppIcon name="arrow" color="#987758" :size="17" />
        </view>
      </button>
    </view>
    <template v-for="group in groups" :key="group.number">
      <view class="section-heading">
        <text class="section-title">{{ group.label }}</text>
        <text class="group-number">{{ group.number }}</text>
      </view>
      <view class="card feature-list">
        <button
          v-for="item in group.items"
          :key="item.name"
          class="list-row"
          @click="go(item.page, item.query)"
        >
          <view class="tile-icon"><AppIcon :name="item.icon" color="#7885ac" /></view>
          <view class="grow">
            <text class="row-title">{{ item.name }}</text>
            <text class="row-subtitle">
              {{ item.page === 'control' ? '依赖设备端，主机模式不可用' : item.description }}
            </text>
          </view>
          <AppIcon name="chevron" :size="16" color="#a1abc0" />
        </button>
      </view>
    </template>
    <text class="feature-footer">文件和文字功能连接 LVGL；相机与录音依赖设备，当前不可用。</text>
  </AppShell>
</template>
<style scoped>
.feature-connection {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 20px;
  font-size: 12px;
}
.feature-primary {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
.feature-primary > button {
  display: block;
  text-align: left;
  padding: 21px 18px 16px;
  border-radius: 21px;
  background: var(--surface);
}
.feature-art {
  height: 55px;
  display: flex;
  gap: 12px;
  align-items: center;
}
.art-line {
  height: 32px;
  width: 42px;
  border-top: 1px solid #a9b8dc40;
  border-bottom: 1px solid #a9b8dc40;
  transform: skewY(-12deg);
}
.feature-title {
  display: block;
  margin-top: 9px;
  font-size: 17px;
  font-weight: 600;
}
.feature-desc {
  display: block;
  font-size: 10px;
  color: var(--muted);
  margin-top: 6px;
}
.feature-bottom {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 23px;
  font-size: 10px;
  color: #667eea;
}
.transfer-feature .feature-bottom {
  color: #987758;
}
.group-number {
  font-size: 12px;
  color: #a2aec2;
  letter-spacing: 2px;
}
.feature-list {
  padding: 0 18px;
}
.feature-footer {
  display: block;
  font-size: 10px;
  text-align: center;
  margin-top: 26px;
  color: var(--muted);
}
</style>
