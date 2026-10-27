<script setup>
import { ref, computed } from 'vue'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import Sheet from '../../components/Sheet.vue'
import { state, device, go, usePairedPage, confirm, toast } from '../../services/device.js'
import { catalog } from '../../services/catalog.js'
usePairedPage()
const panel = ref(''),
  nickname = ref('')
const saved = computed(() => catalog.filter((item) => state.savedApps.includes(item.id)))
function editProfile() {
  nickname.value = state.settings.nickname
  panel.value = 'profile'
}
function saveProfile() {
  if (!nickname.value.trim() || nickname.value.trim().length > 16) {
    toast('昵称须为 1–16 个字符')
    return
  }
  device.settings({ nickname: nickname.value.trim() })
  panel.value = ''
  toast('昵称已保存')
}
</script>
<template>
  <AppShell title="我的" subtitle="按自己的方式，使用眼镜。" active="me">
    <button class="profile-card" @click="editProfile">
      <view class="profile-avatar">{{ state.settings.nickname.slice(0, 1) }}</view>
      <view class="grow">
        <text class="profile-name">{{ state.settings.nickname }}</text>
        <text class="profile-description">本地个人资料 · 无需登录</text>
      </view>
      <AppIcon name="edit" color="#fff" :size="18" />
    </button>
    <view class="profile-stats">
      <button @click="panel = 'saved'">
        <text>{{ saved.length }}</text>
        <text>我的收藏</text>
      </button>
      <button @click="go('files', 'location=phone')">
        <text>{{ state.files.filter((f) => f.location === 'phone').length }}</text>
        <text>手机文件</text>
      </button>
      <button @click="go('transfers')">
        <text>{{ state.transfers.filter((t) => t.status === 'done').length }}</text>
        <text>已完成传输</text>
      </button>
    </view>
    <view class="section-heading"><text class="section-title">设备与连接</text></view>
    <view class="card settings-list">
      <button class="list-row" @click="go('device')">
        <AppIcon name="glasses" color="#8591aa" />
        <view class="grow">
          <text class="row-title">设备管理</text>
          <text class="row-subtitle">
            {{ state.device?.name }} · {{ state.connected ? '已连接' : '离线' }}
          </text>
        </view>
        <AppIcon name="chevron" :size="16" color="#a1abc0" />
      </button>
    </view>
    <view class="section-heading"><text class="section-title">应用偏好</text></view>
    <view class="card settings-list">
      <view class="list-row">
        <AppIcon name="moon" color="#8591aa" />
        <view class="grow"><text class="row-title">界面外观</text></view>
        <view class="theme-selector">
          <button
            :class="{ active: state.settings.theme === 'light' }"
            @click="device.settings({ theme: 'light' })"
          >
            浅色
          </button>
          <button
            :class="{ active: state.settings.theme === 'dark' }"
            @click="device.settings({ theme: 'dark' })"
          >
            深色
          </button>
        </view>
      </view>
      <view
        v-for="item in [
          {
            key: 'notifications',
            label: '传输完成提醒',
            sub: '在应用内显示完成提示',
            icon: 'bell',
          },
          { key: 'compact', label: '紧凑列表', sub: '在同一屏显示更多文件', icon: 'grid' },
        ]"
        :key="item.key"
        class="list-row"
      >
        <AppIcon :name="item.icon" color="#8591aa" />
        <view class="grow">
          <text class="row-title">{{ item.label }}</text>
          <text class="row-subtitle">{{ item.sub }}</text>
        </view>
        <button
          class="switch"
          :class="{ on: state.settings[item.key] }"
          role="switch"
          :aria-checked="state.settings[item.key]"
          :aria-label="item.label"
          @click="device.settings({ [item.key]: !state.settings[item.key] })"
        >
          <view class="switch-knob" />
        </button>
      </view>
    </view>
    <view class="section-heading"><text class="section-title">更多</text></view>
    <view class="card settings-list">
      <button
        class="list-row"
        @click="
          confirm('清理传输记录', '仅移除已完成和已取消的记录，不删除任何文件。', () => {
            device.clearHistory()
            toast('记录已清理')
          })
        "
      >
        <AppIcon name="trash" color="#8591aa" />
        <text class="row-title grow">清理传输记录</text>
        <AppIcon name="chevron" :size="16" color="#a1abc0" />
      </button>
      <button class="list-row" @click="panel = 'about'">
        <AppIcon name="info" color="#8591aa" />
        <text class="row-title grow">关于与隐私</text>
        <text class="small muted">v0.1.0</text>
        <AppIcon name="chevron" :size="16" color="#a1abc0" />
      </button>
    </view>
    <text class="me-footer">
      V821 · 视界
      <br />
      <text>KEEP YOUR WORLD IN SIGHT</text>
    </text>
    <Sheet
      :open="!!panel"
      :title="{ profile: '个人资料', saved: '我的收藏', about: '关于 V821 视界' }[panel]"
      @close="panel = ''"
    >
      <template v-if="panel === 'profile'">
        <text class="field-label">昵称</text>
        <view class="field">
          <input
            v-model="nickname"
            @blur="nickname = $event.detail.value"
            maxlength="16"
            placeholder="输入昵称"
            aria-label="昵称"
          />
        </view>
        <text class="row-subtitle">资料只保存在当前浏览器中。</text>
        <button class="primary wide" style="margin-top: 24px" @click="saveProfile">保存</button>
      </template>
      <template v-if="panel === 'saved'">
        <button
          v-for="item in saved"
          :key="item.id"
          class="list-row"
          @click="item.theme ? device.settings({ theme: item.theme }) : go(item.action, item.query)"
        >
          <view class="tile-icon"><AppIcon :name="item.icon" :color="item.color" /></view>
          <view class="grow">
            <text class="row-title">{{ item.name }}</text>
            <text class="row-subtitle">{{ item.category }} · {{ item.label }}</text>
          </view>
          <AppIcon name="chevron" :size="16" color="#a1abc0" />
        </button>
        <view v-if="!saved.length" class="empty">
          <text class="row-title">还没有收藏</text>
          <text class="row-subtitle">到商店发现感兴趣的工具和主题。</text>
        </view>
      </template>
      <template v-if="panel === 'about'">
        <view class="about-mark"><AppIcon name="glasses" color="#667eea" :size="42" /></view>
        <text class="about-title">V821 · 视界</text>
        <text class="about-text">
          智能眼镜手机伴侣 · 0.1.0
          <br />
          <br />
          此版本连接本机运行的 LVGL，文件与文字会写入其工作目录；相机与麦克风暂不可用。
          <br />
          <br />
          配对凭据、资料与设置保存在当前浏览器，手机文件保存在本地
          IndexedDB。解除绑定后需重新输入眼镜显示的配对码。AI
          与翻译仅发送草稿，由眼镜端确认后调用配置的服务。
        </text>
      </template>
    </Sheet>
  </AppShell>
</template>
<style scoped>
.profile-card {
  display: flex;
  width: 100%;
  padding: 24px 20px;
  gap: 14px;
  text-align: left;
  background: linear-gradient(120deg, #667eea, #8071b0);
  border-radius: 23px;
  color: #fff;
}
.profile-avatar {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 54px;
  height: 54px;
  border-radius: 19px;
  background: #ffffff24;
  border: 1px solid #ffffff40;
  font-size: 23px;
  flex-shrink: 0;
}
.profile-name {
  display: block;
  font-size: 19px;
  font-weight: 600;
  word-break: break-all;
}
.profile-description {
  display: block;
  margin-top: 7px;
  color: #e4e7fb;
  font-size: 10px;
}
.profile-stats {
  display: flex;
  margin: 23px 0 6px;
}
.profile-stats button {
  width: 33.333%;
  flex-direction: column;
  gap: 6px;
  font-size: 21px;
  font-weight: 600;
  border-right: 1px solid var(--border);
}
.profile-stats button:last-child {
  border-right: none;
}
.profile-stats button text:last-child {
  font-size: 10px;
  color: var(--muted);
  font-weight: normal;
}
.settings-list {
  padding: 0 18px;
}
.settings-list .row-title {
  font-size: 13px;
}
.settings-list .row-subtitle {
  font-size: 10px;
}
.theme-selector {
  display: flex;
  background: var(--input);
  padding: 3px;
  border-radius: 9px;
}
.theme-selector button {
  font-size: 10px;
  padding: 5px 9px;
  border-radius: 7px;
  color: var(--muted);
}
.theme-selector button.active {
  color: #667eea;
  background: var(--surface);
  box-shadow: 0 1px 4px #2432470c;
}
.me-footer {
  text-align: center;
  color: #9aa5b8;
  display: block;
  margin-top: 30px;
  font-size: 13px;
  line-height: 2;
}
.me-footer text {
  font-size: 8px;
  letter-spacing: 2px;
}
.about-mark {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 80px;
  height: 80px;
  border-radius: 25px;
  background: var(--soft);
  margin: 20px auto 15px;
}
.about-title {
  display: block;
  text-align: center;
  font-size: 21px;
  font-weight: 600;
}
.about-text {
  display: block;
  margin-top: 20px;
  line-height: 1.9;
  font-size: 12px;
  color: var(--muted);
}
</style>
