# 项目文档

本目录集中维护系统架构、运行模式和项目展示资源。模块的构建、配置、接口及部署说明保留在对应源码目录。

## 系统架构

| 文档 | 内容 |
| --- | --- |
| [V821 运行模式](architecture/runtime-modes.md) | 常电 UI 与 JL 主控受控模式、媒体资源所有权、启动入口及固件约束 |
| [Linux 主机模拟器架构](architecture/host-simulator.md) | 源码复用、构建与进程、线程模型、显示与输入、通信、配对、存储及测试边界 |

## 开发与部署

| 模块 | 文档 |
| --- | --- |
| Linux 主机模拟器 | [环境准备、构建、远程预览与测试](../Application/host-debug/README.md) |
| LVGL 眼镜界面 | [集成、配置与手机通信协议](../Application/lvgl-home/README.md) · [显示布局](../Application/lvgl-home/docs/layout.md) |
| 手机伴侣 | [uni-app H5 开发与主机联调](../Application/mobile-app/README.md) · [素材与资源](../Application/mobile-app/docs/ASSETS.md) |
| V821 本地媒体后端 | [MPP API、资源管理与音频配置](../Application/mpp-media/README.md) |
| JL 主控受控应用 | [构建、部署与打包](../Application/aiglass-apps/README.md) · [源码清单](../Application/aiglass-apps/MANIFEST.md) |

## 受控模式集成

| 文档 | 内容 |
| --- | --- |
| [AGLink 协议](../Application/aiglass-apps/integration/AGLINK_PROTOCOL.md) | 控制端与 V821 的传输路径、命令、载荷及完成时序 |
| [受控媒体 API](../Application/aiglass-apps/integration/CONTROLLED_MEDIA.md) | `libaiglass_media` 的接口、状态与资源约束 |
| [媒体接入选择](../Application/aiglass-apps/integration/LVGL_MEDIA.md) | 常电 UI 与受控媒体接口的适用范围 |
| [BSP 依赖](../Application/aiglass-apps/integration/BSP_DEPENDENCIES.md) | Tina V821 BSP、工具链与媒体库依赖 |

这些集成文档随 `aiglass-apps` 独立发布，保留在该模块的 `integration` 目录。

## 展示资源

| 资源 | 文件 |
| --- | --- |
| 中文系统架构图 | [PNG](images/SMART_AI_GLASSES_ZH.png) · [SVG](images/SMART_AI_GLASSES_ZH.svg) |
| 英文系统架构图 | [PNG](images/SMART_AI_GLASSES_EN.png) · [SVG](images/SMART_AI_GLASSES_EN.svg) |
| Linux 主机联调画面 | [Simulate.png](images/Simulate.png) |

系统架构图描述整机方案；软件实现与验证范围以模块文档为准。
主 README 使用 PNG 预览，SVG 用于矢量查看。更新图示时须同步生成对应语言的 PNG，保持两个版本内容一致。
SVG 中的文字已转为矢量轮廓，不依赖脚本、系统字体或外部资源。重新导出时应保持这一兼容性。
