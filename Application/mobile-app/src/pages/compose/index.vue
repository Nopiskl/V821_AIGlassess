<script setup>
import { ref, computed } from 'vue'
import { onLoad } from '@dcloudio/uni-app'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import { state, device, usePairedPage, toast } from '../../services/device.js'
usePairedPage()
const kind = ref('note'),
  content = ref(''),
  problem = ref('')
const types = {
  note: {
    title: '笔记同步',
    subtitle: '写在手机上，留在视线里。',
    icon: 'note',
    hint: '记下一个灵感、一份清单或一段文字…',
    button: '同步到眼镜笔记',
    description: '同步后在眼镜笔记目录中生成文本文件。',
  },
  notify: {
    title: '消息投送',
    subtitle: '把一条提醒，送到眼前。',
    icon: 'message',
    hint: '输入需要提醒自己的内容…',
    button: '发送提醒',
    description: '文字发送到眼镜并显示在消息区，不读取手机系统通知。',
  },
  chat: {
    title: 'AI 对话草稿',
    subtitle: '想问的，就从这里开始。',
    icon: 'spark',
    hint: '写下想向眼镜 AI 提出的问题…',
    button: '发送对话草稿',
    description: '草稿发送到 LVGL 对话页，由眼镜端确认发送。',
  },
  translate: {
    title: '翻译草稿',
    subtitle: '换一种语言，看同一个世界。',
    icon: 'translate',
    hint: '输入希望翻译的内容…',
    button: '发送翻译草稿',
    description: '草稿发送到 LVGL 翻译页，由眼镜端确认发送。',
  },
  navigate: {
    title: '导航卡片',
    subtitle: '下一程，清晰一点。',
    icon: 'compass',
    hint: '例如：前方 200 米右转，进入海滨路。',
    button: '发送路线文字',
    description: '只投送编辑好的路线文字，不调用地图、定位或路线规划。',
  },
}
const config = computed(() => types[kind.value])
const bytes = computed(() => new TextEncoder().encode(content.value).length)
const recent = computed(() => state.messages.filter((m) => m.kind === kind.value).slice(0, 3))
async function send() {
  problem.value = ''
  try {
    await device.send(kind.value, content.value)
    toast('LVGL 已接收')
    content.value = ''
  } catch (e) {
    problem.value = e.message
  }
}
onLoad((options) => {
  if (types[options.kind]) kind.value = options.kind
  if (options.preset === 'travel')
    content.value =
      kind.value === 'translate'
        ? '你好，请问去海边的步道怎么走？'
        : '周末出行清单\n\n眼镜与充电线\n水杯与防晒\n一双适合散步的鞋\n\n留一点时间，看看沿途的风景。'
})
</script>
<template>
  <AppShell :title="config.title" :subtitle="config.subtitle" previous>
    <view class="compose-preview">
      <view class="row between">
        <view class="row" style="gap: 6px">
          <AppIcon :name="config.icon" color="#b9cbf7" :size="15" />
          <text>眼镜内容预览</text>
        </view>
        <text>LVGL</text>
      </view>
      <text class="preview-content">{{ content || '你的文字，将显示在这里。' }}</text>
      <view class="preview-rule" />
    </view>
    <view class="section-heading">
      <text class="section-title">编辑内容</text>
      <text :class="['small', bytes > 2047 ? 'over-limit' : 'muted']">{{ bytes }} / 2047 字节</text>
    </view>
    <view class="card compose-card">
      <textarea
        v-model="content"
        @blur="content = $event.detail.value"
        class="compose-input"
        :placeholder="config.hint"
        :maxlength="2047"
        aria-label="发送内容"
      />
      <view class="row between">
        <text class="small muted">纯文本</text>
        <button class="text-button" :disabled="!content" @click="content = ''">清空</button>
      </view>
    </view>
    <text v-if="problem" class="form-error" role="alert">{{ problem }}</text>
    <button
      class="primary wide"
      style="margin-top: 20px"
      :disabled="!content.trim() || bytes > 2047 || !state.connected"
      @click="send"
    >
      {{ config.button }}
      <AppIcon name="arrow" :size="18" color="#fff" />
    </button>
    <view class="notice" style="margin-top: 18px">{{ config.description }}</view>
    <view v-if="recent.length">
      <view class="section-heading"><text class="section-title">最近发送</text></view>
      <view class="card recent-messages">
        <view v-for="message in recent" :key="message.id" class="message-record">
          <text>{{ message.text }}</text>
          <text>{{ message.date }} · LVGL 已接收</text>
        </view>
      </view>
    </view>
  </AppShell>
</template>
<style scoped>
.compose-preview {
  background: linear-gradient(120deg, #283851, #33304a);
  border-radius: 21px;
  color: #a6b7dc;
  padding: 20px;
  font-size: 9px;
  letter-spacing: 1px;
}
.preview-content {
  white-space: pre-wrap;
  word-break: break-word;
  display: block;
  min-height: 68px;
  max-height: 135px;
  overflow-y: auto;
  color: #e5ecfb;
  font-size: 15px;
  line-height: 1.9;
  margin: 26px 0 16px;
  letter-spacing: 0;
}
.preview-rule {
  height: 2px;
  width: 24px;
  background: #8799c5;
}
.compose-card {
  padding: 16px;
}
.compose-input {
  width: 100%;
  height: 175px;
  font-size: 13px;
  line-height: 1.8;
}
.over-limit {
  color: #c75448;
}
.recent-messages {
  padding: 0 18px;
}
.message-record {
  border-bottom: 1px solid var(--border);
  padding: 16px 0;
}
.message-record:last-child {
  border-bottom: 0;
}
.message-record text:first-child {
  display: block;
  white-space: pre-wrap;
  word-break: break-word;
  font-size: 12px;
  max-height: 75px;
  overflow: hidden;
  line-height: 1.8;
}
.message-record text:last-child {
  display: block;
  font-size: 9px;
  color: var(--muted);
  margin-top: 8px;
}
</style>
