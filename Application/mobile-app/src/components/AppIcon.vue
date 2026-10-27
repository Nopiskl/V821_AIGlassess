<script setup>
import { computed } from 'vue'
const props = defineProps({
  name: { type: String, default: 'spark' },
  size: { type: [Number, String], default: 22 },
  color: { type: String, default: '#22304a' },
})
const paths = {
  home: '<path d="m3 10 9-7 9 7v10H3Z"/><path d="M9 20v-7h6v7"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="14" y="14" width="7" height="7" rx="2"/>',
  store:
    '<path d="M4 9h16l-2-5H6ZM5 10v10h14V10M9 20v-6h6v6"/><path d="M4 9c0 4 4 4 4 0 0 4 4 4 4 0 0 4 4 4 4 0 0 4 4 4 4 0"/>',
  user: '<circle cx="12" cy="8" r="4"/><path d="M4 21v-2a8 8 0 0 1 16 0v2"/>',
  arrow: '<path d="M5 12h14m-6-6 6 6-6 6"/>',
  chevron: '<path d="m9 5 7 7-7 7"/>',
  back: '<path d="m14 5-7 7 7 7"/>',
  close: '<path d="m6 6 12 12M6 18 18 6"/>',
  check: '<path d="m5 12 4 4L19 6"/>',
  plus: '<path d="M12 4v16M4 12h16"/>',
  wifi: '<path d="M3 8a15 15 0 0 1 18 0M6 12a10 10 0 0 1 12 0M9 16a5 5 0 0 1 6 0"/><circle cx="12" cy="20" r=".7"/>',
  glasses:
    '<rect x="2" y="9" width="8" height="7" rx="3"/><rect x="14" y="9" width="8" height="7" rx="3"/><path d="M10 11h4M2 10l2-5M22 10l-2-5"/>',
  battery:
    '<rect x="2" y="7" width="18" height="10" rx="2"/><path d="M23 10v4M5 10v4m3-4v4m3-4v4m3-4v4"/>',
  folder: '<path d="M3 7V4h7l3 3h8v13H3Z"/><path d="M3 10h18"/>',
  transfer: '<path d="M3 7h17m-5-4 5 4-5 4M21 17H4m5-4-5 4 5 4"/>',
  camera: '<path d="M3 7h4l2-3h6l2 3h4v13H3Z"/><circle cx="12" cy="13" r="4"/>',
  video: '<rect x="2" y="5" width="13" height="14" rx="3"/><path d="m15 10 6-4v12l-6-4"/>',
  mic: '<rect x="9" y="3" width="6" height="12" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3M8 21h8"/>',
  note: '<path d="M5 3h10l4 4v14H5ZM14 3v5h5M8 12h8M8 16h6"/>',
  message: '<path d="M3 4h18v13H9l-6 4Z"/><path d="M7 9h10M7 13h6"/>',
  translate: '<path d="M2 6h12M8 3v3M5 6c0 5 4 8 8 9M11 6c0 5-4 8-8 9M13 21l4-11 4 11M15 17h4"/>',
  compass: '<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5Z"/>',
  spark: '<path d="m12 3 2.5 6.5L21 12l-6.5 2.5L12 21l-2.5-6.5L3 12l6.5-2.5Z"/>',
  search: '<circle cx="10" cy="10" r="6"/><path d="m15 15 6 6"/>',
  download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
  upload: '<path d="M12 16V4m-5 5 5-5 5 5M4 16v5h16v-5"/>',
  trash: '<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7M14 10v7"/>',
  edit: '<path d="m4 16 12-12 4 4L8 20H4ZM13 7l4 4"/>',
  pause: '<path d="M8 4v16M16 4v16"/>',
  play: '<path d="m7 4 13 8-13 8Z"/>',
  refresh: '<path d="M20 8a8 8 0 1 0 0 8M20 3v6h-6"/>',
  bell: '<path d="M5 17h14l-2-4V9a5 5 0 0 0-10 0v4ZM10 21h4"/>',
  shield: '<path d="m12 2 8 4v7c0 4-8 9-8 9S4 17 4 13V6Z"/><path d="m8 12 3 3 5-6"/>',
  moon: '<path d="M20 15A9 9 0 0 1 9 4a9 9 0 1 0 11 11Z"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v1"/>',
  lab: '<path d="M8 3h8M9 3v6L3 19q-1 2 2 2h14q3 0 2-2L15 9V3M7 14h10"/>',
  link: '<path d="m9 15 6-6M9 8l3-3a5 5 0 0 1 7 7l-3 3M15 16l-3 3a5 5 0 0 1-7-7l3-3"/>',
  heart: '<path d="M12 21 3 12C-2 5 7 0 12 7c5-7 14-2 9 5Z"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
  image:
    '<rect x="3" y="3" width="18" height="18" rx="3"/><circle cx="8" cy="8" r="1"/><path d="m3 17 6-6 4 4 3-3 5 5"/>',
  settings:
    '<path d="M4 6h16M4 12h16M4 18h16"/><circle cx="8" cy="6" r="2" fill="white"/><circle cx="16" cy="12" r="2" fill="white"/><circle cx="10" cy="18" r="2" fill="white"/>',
}
const source = computed(
  () =>
    'data:image/svg+xml;charset=utf-8,' +
    encodeURIComponent(
      `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="${props.color}" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round">${paths[props.name] || paths.spark}</svg>`,
    ),
)
</script>
<template>
  <image
    :src="source"
    :style="{ width: `${size}px`, height: `${size}px` }"
    mode="aspectFit"
    class="app-icon"
    aria-hidden="true"
  />
</template>
<style scoped>
.app-icon {
  display: inline-block;
  flex-shrink: 0;
  vertical-align: middle;
}
</style>
