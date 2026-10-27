<script setup>
import AppIcon from './AppIcon.vue'
defineProps({ open: Boolean, title: String })
const emit = defineEmits(['close'])
</script>
<template>
  <view v-if="open" class="sheet-overlay" @click.self="emit('close')">
    <view class="sheet" role="dialog" aria-modal="true" :aria-label="title">
      <view class="sheet-handle" />
      <view class="section-heading">
        <text class="section-title">{{ title }}</text>
        <button class="icon-button" aria-label="关闭" @click="emit('close')">
          <AppIcon name="close" />
        </button>
      </view>
      <slot />
    </view>
  </view>
</template>
