<script setup>
import { onLaunch } from '@dcloudio/uni-app'
import { startDevice } from './services/device.js'
onLaunch(startDevice)
</script>

<style>
@import './styles/theme.css';
</style>
