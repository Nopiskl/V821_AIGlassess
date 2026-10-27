# AIGlass 应用与来源清单

`apps/` 包含独立维护和发布的应用源码，初始内容于 2026-10-21 从
`platform/allwinner/eyesee-apps/demo_ai_glass` 导入。
构建使用本目录源码，以只读方式引用 BSP sysroot。`apps/src/Makefile` 将 AGLink 头文件
和 `libaglink.so` 安装至私有 `build/pkg-install`，构建过程不写入 BSP staging。

本仓库于 2026-10-22 从 `aiglass-apps-1.0.0.tar.gz` 导入到
`Application/aiglass-apps`，原始归档 SHA-256：

```text
c5f5086b2133776c024d26646cf2bae38efe0e236ca729ebba7c9edb1eaeefdf
```

## 组件职责

- `Application/Makefile` 提供统一构建入口；`envsetup.sh` 在依赖检查失败时返回非零状态。
- 直播应用按 MPP 配置构建，依赖 BSP 的编解码、检测及媒体库。
- `apps/src/media` 提供受控媒体接口，复用 `take_video.c` 的 RT Media/MPP 管线及资源生命周期。
- 七个受控应用使用运行模式锁和媒体锁，通过 `v821-mode.sh` 选择入口；录音和录像处理停止、断线及退出时的资源释放。
- 常电 UI 使用独立的 `mpp-media` 模块。应用包保留来源归档中的预编译静态库，其内容保持原样。

| 构建目标 | 本包产物 | BSP 侧主要依赖 |
| --- | --- | --- |
| `lib` | `/lib/libaglink.so` | 内核 AGLink UAPI |
| `media` | `output/dev/lib/libaiglass_media.a`、开发头文件与链接参数 | RT Media、Eyesee MPP、AGLink |
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

上述路径相对于仓库根目录。复制进完整 BSP 时，`device/` 保持同级路径，
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

发布包排除本机配置和生成物：`local.mk`、`build/`、`output/`、`dist/`。发布脚本检查
全部入包文件，遇到软链接时拒绝打包。
