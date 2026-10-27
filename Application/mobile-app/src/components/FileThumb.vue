<script setup>
import AppIcon from './AppIcon.vue'
defineProps({ file: Object, large: Boolean })
</script>
<template>
  <view class="file-thumb" :class="[file.kind, { large }]">
    <image v-if="file.asset" :src="file.asset" mode="aspectFill" />
    <AppIcon
      v-else
      :name="{ photo: 'image', video: 'video', audio: 'mic', note: 'note' }[file.kind]"
      :size="large ? 42 : 24"
      :color="file.kind === 'audio' ? '#a66c41' : '#5472a1'"
    />
  </view>
</template>
