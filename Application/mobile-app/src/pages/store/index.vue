<script setup>
import { ref, computed } from 'vue'
import AppShell from '../../components/AppShell.vue'
import AppIcon from '../../components/AppIcon.vue'
import Sheet from '../../components/Sheet.vue'
import { catalog } from '../../services/catalog.js'
import { state, device, go, usePairedPage, toast } from '../../services/device.js'
usePairedPage()
const search = ref(''),
  filter = ref('全部'),
  selected = ref(null)
const list = computed(() =>
  catalog.filter(
    (item) =>
      (filter.value === '全部' || item.category === filter.value) &&
      (item.name + item.description).includes(search.value.trim()),
  ),
)
function useItem(item) {
  selected.value = null
  if (item.theme) {
    device.settings({ theme: item.theme })
    toast('手机界面主题已更新')
  } else go(item.action, item.query)
}
</script>
<template>
  <AppShell title="发现更多可能" eyebrow="为你的眼镜生活，添一点新意。" active="store">
    <view class="search-field">
      <AppIcon name="search" color="#9aa5b8" :size="19" />
      <input v-model="search" placeholder="搜索工具、主题或模板" aria-label="搜索商店" />
    </view>
    <button v-if="!search" class="store-featured" @click="selected = catalog[0]">
      <view class="store-orbit orbit-a" />
      <view class="store-orbit orbit-b" />
      <text class="featured-tag">本周灵感 / COLLECTION 01</text>
      <text class="featured-title">
        轻装出发。
        <br />
        把世界，装进视野。
      </text>
      <text class="featured-desc">一份清单，开启下一段旅程。</text>
      <view class="featured-bottom">
        <text>探索出行模板</text>
        <view class="featured-arrow"><AppIcon name="arrow" color="#fff" :size="18" /></view>
      </view>
      <view class="travel-symbol"><AppIcon name="compass" color="#dee5ff" :size="68" /></view>
    </button>
    <view class="chips">
      <button
        v-for="category in ['全部', '工具', '主题', '模板']"
        :key="category"
        class="chip"
        :class="{ active: filter === category }"
        @click="filter = category"
      >
        {{ category }}
      </button>
    </view>
    <view class="section-heading" style="margin-top: 22px">
      <text class="section-title">为你精选</text>
      <text class="pill neutral">本地内容目录</text>
    </view>
    <view class="store-list">
      <button v-for="item in list" :key="item.id" class="store-item" @click="selected = item">
        <view class="store-icon" :style="{ background: item.color + '16' }">
          <AppIcon :name="item.icon" :color="item.color" :size="28" />
        </view>
        <view class="grow">
          <view class="row" style="gap: 7px">
            <text class="row-title">{{ item.name }}</text>
            <text v-if="state.savedApps.includes(item.id)" class="saved-dot">已收藏</text>
          </view>
          <text class="row-subtitle">{{ item.description }}</text>
          <text class="item-tag">{{ item.category }} · {{ item.tag }}</text>
        </view>
        <AppIcon name="plus" :size="17" color="#8b98b4" />
      </button>
    </view>
    <view v-if="!list.length" class="empty">
      <text class="row-title">没有找到相关内容</text>
      <text class="row-subtitle">试试其他关键词或分类。</text>
    </view>
    <text class="store-footnote">收藏与主题仅保存在本地，暂不提供眼镜应用安装。</text>
    <Sheet :open="!!selected" :title="selected?.name" @close="selected = null">
      <template v-if="selected">
        <view class="detail-icon" :style="{ background: selected.color + '16' }">
          <AppIcon :name="selected.icon" :color="selected.color" :size="40" />
        </view>
        <text class="pill">{{ selected.tag }}</text>
        <text class="detail-description">{{ selected.detail }}</text>
        <view class="button-row">
          <button class="secondary" @click="device.saveApp(selected.id)">
            <AppIcon name="heart" :size="18" color="#667eea" />
            {{ state.savedApps.includes(selected.id) ? '取消收藏' : '收藏' }}
          </button>
          <button class="primary" @click="useItem(selected)">{{ selected.label }}</button>
        </view>
      </template>
    </Sheet>
  </AppShell>
</template>
<style scoped>
.store-featured {
  display: block;
  position: relative;
  overflow: hidden;
  text-align: left;
  padding: 23px;
  border-radius: 23px;
  margin-top: 20px;
  width: 100%;
  background: linear-gradient(120deg, #667eea, #7268b2);
  color: white;
}
.featured-tag {
  position: relative;
  display: block;
  letter-spacing: 1px;
  font-size: 8px;
  color: #dee4ff;
}
.featured-title {
  position: relative;
  z-index: 1;
  display: block;
  font-size: 23px;
  font-weight: 650;
  line-height: 1.6;
  margin-top: 20px;
  letter-spacing: -0.5px;
}
.featured-desc {
  display: block;
  position: relative;
  z-index: 1;
  font-size: 10px;
  color: #e0e5ff;
  margin-top: 10px;
}
.featured-bottom {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 22px;
  font-size: 10px;
  position: relative;
  z-index: 1;
}
.featured-arrow {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 27px;
  height: 27px;
  border: 1px solid #ffffff4d;
  border-radius: 50%;
}
.store-orbit {
  position: absolute;
  border: 1px solid #ffffff26;
  width: 230px;
  height: 230px;
  border-radius: 50%;
  right: -90px;
  top: 35px;
}
.orbit-b {
  width: 180px;
  height: 180px;
  top: 60px;
  right: -65px;
}
.travel-symbol {
  position: absolute;
  right: 10px;
  top: 77px;
  transform: rotate(20deg);
  opacity: 0.45;
}
.store-list {
  background: var(--surface);
  padding: 0 18px;
  border-radius: 21px;
}
.store-item {
  width: 100%;
  padding: 20px 0;
  border-bottom: 1px solid var(--border);
  gap: 13px;
  text-align: left;
}
.store-item:last-child {
  border-bottom: none;
}
.store-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 52px;
  height: 52px;
  border-radius: 17px;
  flex-shrink: 0;
}
.store-item .row-subtitle {
  font-size: 10px;
  line-height: 1.7;
}
.item-tag {
  display: block;
  font-size: 8px;
  color: #98a3b7;
  margin-top: 7px;
}
.saved-dot {
  color: #667eea;
  font-size: 9px;
}
.store-footnote {
  display: block;
  text-align: center;
  font-size: 10px;
  color: var(--muted);
  line-height: 1.8;
  margin-top: 23px;
}
.detail-icon {
  width: 88px;
  height: 88px;
  border-radius: 25px;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 10px 0 20px;
}
.detail-description {
  display: block;
  line-height: 1.9;
  color: var(--muted);
  font-size: 13px;
  margin-top: 18px;
}
</style>
