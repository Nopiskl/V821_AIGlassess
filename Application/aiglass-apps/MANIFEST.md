# AIGlass 应用与来源清单

`apps/` 是可独立发布的实体源码，不是指向 BSP 的软链接。初始内容于 2026-10-21 从
`platform/allwinner/eyesee-apps/demo_ai_glass` 导入；从此可以在本补丁包中独立演进。
为保证 BSP sysroot 只读，外部副本的 `apps/src/Makefile` 已调整为仅把 AGLink 头文件
和 `libaglink.so` 写入私有 `build/pkg-install`，不再写回 BSP staging。

本仓库于 2026-10-22 从 `aiglass-apps-1.0.0.tar.gz` 导入到
`Application/aiglass-apps`，原始归档 SHA-256：

```text
c5f5086b2133776c024d26646cf2bae38efe0e236ca729ebba7c9edb1eaeefdf
```

导入后增加了 `Application/Makefile` 统一入口，补齐直播应用的 MPP 配置读取及检查、
BSP 编解码/检测依赖，并让 `envsetup.sh` 在依赖检查失败时返回失败。
应用 C/C++ 源码和预编译静态库保持原样。

| 构建目标 | 本包产物 | BSP 侧主要依赖 |
| --- | --- | --- |
| `lib` | `/lib/libaglink.so` | 内核 AGLink UAPI |
| `normal` | `/bin/ai_glass_normal` | wirelesscommon |
| `photo` | `/bin/ai_glass_photo` | wirelesscommon |
| `audio` | `/bin/ai_glass_audio` | wirelesscommon |
| `download` | `/bin/ai_glass_download` | wirelesscommon、wifimanager |
| `ota` | `/bin/ai_glass_ota` | wirelesscommon、wifimanager |
| `video` | `/bin/ai_glass_video` | RT Media、Eyesee MPP、libawion |
| `livestream` | `/bin/ai_glass_livestream` | RT Media、Eyesee MPP、RTSP、PDET、wifimanager |

## BSP 配置关联（仅作兼容性参考）

- `SDK/TinaLinux 5.0/device/config/chips/v821/configs/aiglass_fastboot/quick_config.json` 中的 `ag_glass_app`
  打开内核 AG Link/AG UART，并启用 AIGlass 用户态应用；
- `SDK/TinaLinux 5.0/openwrt/target/v821-aiglass_fastboot/defconfig` 默认选中
  `ai-glass`、`ai-glass-lib` 及 7 个子应用；普通 `v821-aiglass/defconfig`
  未启用 `ai-glass` 和 RT Media；
- `SDK/TinaLinux 5.0/device/config/chips/v821/configs/aiglass_fastboot/ramdisk_list.json` 中的 `demo_video_in`
  属于 BSP 的 `rt_media`，不是本包的 AIGlass 主程序；
- `low_power_app` 涉及的 `demo_video_rtsp`/`demo_video_webrtc` 是独立低功耗演示链路，
  不纳入本应用包。

这些是本仓库的补丁路径。复制进完整 BSP 时，`device/` 保持同级路径，
`openwrt/target/v821-aiglass_fastboot` 对应完整 BSP 中的
`openwrt/target/v821/v821-aiglass_fastboot`。板级补丁没有复制进应用源码发布包；
应用包本身的源码入口始终是本目录下的 `apps/`。

## 发布范围

`make release` 包含：

- `apps/` 完整实体源码和源码内所需预编译库；
- 顶层 `Makefile`、`envsetup.sh`、`config.mk.example`、`VERSION`；
- `scripts/` 构建/发布工具；
- `integration/` 依赖边界文档；
- 本清单和 `README.md`。

不会包含本机配置和生成物：`local.mk`、`build/`、`output/`、`dist/`。发布脚本会检查
全部入包文件并拒绝任何软链接。
