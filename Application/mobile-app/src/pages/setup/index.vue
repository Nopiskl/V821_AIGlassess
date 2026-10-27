<script setup>
import { ref } from 'vue'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import { device, state, tab } from '../../services/device.js'

const stage = ref(0)
const candidate = ref(null)
const code = ref('')
const busy = ref(false)
const problem = ref('')
function previousStep() {
  stage.value = 0
  problem.value = ''
  code.value = ''
}
async function perform(action) {
  busy.value = true
  problem.value = ''
  try {
    await action()
  } catch (error) {
    problem.value = error.message
  } finally {
    busy.value = false
  }
}
const discover = () =>
  perform(async () => {
    candidate.value = await device.discover()
    stage.value = 1
  })
const pair = () =>
  perform(async () => {
    await device.pair({ deviceId: candidate.value.id, code: code.value.trim() })
    code.value = ''
    stage.value = 2
  })
const reconnect = () =>
  perform(async () => {
    await device.connect(true)
    tab('home')
  })
</script>
<template>
  <AppShell title="连接你的眼镜" subtitle="核对设备，在眼镜与手机之间建立连接。">
    <view class="setup-steps">
      <view
        v-for="(label, i) in ['发现设备', '核对配对码', '连接完成']"
        :key="label"
        class="setup-step"
        :class="{ current: stage === i, complete: stage > i }"
      >
        <text class="step-number">{{ stage > i ? '✓' : '0' + (i + 1) }}</text>
        <text>{{ label }}</text>
      </view>
    </view>
    <view class="setup-art">
      <image src="/static/glasses-product.png" mode="aspectFit" alt="智能眼镜正面示意图" />
      <text class="art-caption">V821 AI GLASSES</text>
    </view>
    <template v-if="stage === 0">
      <text class="setup-title">先在眼镜端打开设备配对</text>
      <text class="setup-description">
        首次启动时，LVGL 会显示配对码。
        <br />
        查找设备后，在手机上输入这组数字。
      </text>
      <button class="primary wide" :disabled="busy" @click="discover">
        <AppIcon name="search" color="#fff" :size="19" />
        {{ busy ? '正在查找…' : '查找设备' }}
      </button>
      <button
        v-if="device.hasBinding()"
        class="secondary wide"
        style="margin-top: 12px"
        :disabled="busy"
        @click="reconnect"
      >
        恢复连接
      </button>
      <text class="setup-help">当前连接 Linux 主机上运行的 LVGL，设备端配网暂未接入。</text>
    </template>
    <template v-else-if="stage === 1">
      <view class="card">
        <view class="row">
          <view class="tile-icon"><AppIcon name="glasses" color="#667eea" /></view>
          <view class="grow">
            <text class="row-title">{{ candidate.name }}</text>
            <text class="row-subtitle">设备标识 {{ candidate.id.slice(0, 8) }}</text>
          </view>
        </view>
        <text v-if="candidate.paired" class="connection-hint">
          此设备已绑定。使用本浏览器的“恢复连接”，或先在 LVGL 的“设备配对”页面解除原绑定。
        </text>
        <text class="field-label">眼镜屏幕上的配对码</text>
        <view class="field">
          <input
            v-model="code"
            @blur="code = $event.detail.value"
            type="text"
            inputmode="numeric"
            maxlength="6"
            placeholder="输入 6 位数字"
            aria-label="配对码"
            :disabled="busy"
          />
        </view>
        <text class="connection-hint">
          设备标识应与 LVGL 屏幕一致。配对成功后，下次打开会自动恢复连接。
        </text>
      </view>
      <view class="button-row">
        <button class="secondary" :disabled="busy" @click="previousStep">上一步</button>
        <button class="primary" :disabled="busy" @click="pair">
          {{ busy ? '正在配对…' : '确认配对' }}
        </button>
      </view>
      <button
        v-if="device.hasBinding()"
        class="text-button wide"
        style="margin-top: 12px"
        :disabled="busy"
        @click="reconnect"
      >
        恢复连接
      </button>
    </template>
    <template v-else>
      <view class="card paired-card">
        <AppIcon name="check" color="#667eea" :size="32" />
        <text class="setup-title">配对完成</text>
        <text class="setup-description">
          {{ state.device?.name }}
          <br />
          可以开始管理文件、同步笔记和发送消息。
        </text>
      </view>
      <button class="primary wide" style="margin-top: 24px" @click="tab('home')">
        进入主页面
        <AppIcon name="arrow" color="#fff" :size="18" />
      </button>
    </template>
    <text v-if="problem" class="form-error" role="alert">{{ problem }}</text>
  </AppShell>
</template>
<style scoped>
.setup-steps {
  display: flex;
  gap: 10px;
  margin: 28px 0 10px;
}
.setup-step {
  flex: 1;
  border-top: 2px solid var(--border);
  padding-top: 10px;
  font-size: 11px;
  color: var(--muted);
  display: flex;
  gap: 7px;
}
.setup-step.current {
  color: #667eea;
  border-color: #667eea;
  font-weight: 600;
}
.setup-step.complete {
  border-color: #a5b5f3;
}
.step-number {
  font-size: 10px;
  letter-spacing: 1px;
}
.setup-art {
  height: 205px;
  position: relative;
  display: flex;
  justify-content: center;
  align-items: center;
}
.setup-art image {
  width: 100%;
  height: 195px;
}
.art-caption {
  position: absolute;
  bottom: 3px;
  font-size: 9px;
  letter-spacing: 2px;
  color: var(--muted);
}
.setup-title {
  display: block;
  text-align: center;
  font-size: 19px;
  font-weight: 650;
  margin-top: 20px;
}
.setup-description {
  display: block;
  text-align: center;
  color: var(--muted);
  font-size: 12px;
  line-height: 1.9;
  margin: 12px 0 24px;
}
.setup-help {
  text-align: center;
  display: block;
  font-size: 11px;
  line-height: 1.8;
  color: var(--muted);
  margin-top: 17px;
}
.connection-hint {
  display: block;
  color: var(--muted);
  font-size: 12px;
  line-height: 1.8;
  margin-top: 16px;
}
.paired-card {
  text-align: center;
}
</style>
