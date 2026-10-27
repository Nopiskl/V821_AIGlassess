# AIGlass Applications

JL 主控、V821 不显示模式下使用的 Linux 应用及 AGLink 通信库，包含 normal、拍照、录音、下载、OTA、录像和直播。
常电 LVGL 模式使用独立的本地 MPP 后端，见[运行模式](../RUNTIME_MODES.md)。
V821 端消息与时序见[AGLink 协议](integration/AGLINK_PROTOCOL.md)。本仓库不包含 JL 端协议实现。

## 构建

依赖完整的 Tina V821 BSP、RISC-V musl 工具链和 BSP 开发库。BSP 路径应不含空格；
仓库内的 `SDK/TinaLinux 5.0` 为板级补丁目录。

在仓库根目录执行，默认板型为 `aiglass_fastboot`：

```bash
export AIGLASS_BSP_ROOT=/path/to/tina-v821-release

make -C Application check
make -C Application all
make -C Application package
```

单独编译应用时，将 `all` 替换为 `normal`、`photo`、`audio`、`download`、`ota`、
`video` 或 `livestream`。

`media` 目标生成可选的受控媒体 C 接口 `libaiglass_media.a`，供自定义受控宿主使用。
接口与控制端边界见[受控媒体接口](integration/CONTROLLED_MEDIA.md)。`all` 同时生成该开发库；
常电 LVGL 使用独立的 `Application/mpp-media`，不链接此库。

首次准备 BSP 依赖使用 `make -C Application bsp-prepare`。该目标会执行 `lunch`
并更新 BSP 配置；已选择对应板型时使用 `make -C Application bsp-deps`。

## 配置

复制 `config.mk.example` 为 `local.mk` 可保存本机配置，常用变量如下：

| 变量 | 用途 |
| --- | --- |
| `AIGLASS_BSP_ROOT` | 完整 BSP 路径 |
| `AIGLASS_BOARD` | 目标板型 |
| `AIGLASS_JOBS` | 并行编译数 |
| `AIGLASS_BUILD_DIR` | 编译目录 |
| `AIGLASS_OUTPUT_DIR` | 产物目录 |
| `AIGLASS_STRIP_OUTPUT` | 设为 `0` 保留调试符号 |

直接在本目录执行 `make` 时，默认板型为 `aiglass`，BSP 路径默认为上级目录。
普通 `aiglass` 配置需启用 `ai-glass` 和 RT Media。切换板型前执行 `distclean`，
或使用独立的编译及产物目录。

## 产物

以下路径相对于本目录：

| 路径 | 内容 |
| --- | --- |
| `build/` | 编译副本、开发头文件和库 |
| `output/rootfs/` | `bin/ai_glass_*`、`lib/libaglink.so`、`etc/media/*.sh` |
| `output/dev/` | `ag_media.h`、`libaiglass_media.a` 和 BSP 链接参数，供受控模式自定义宿主使用 |
| `output/aiglass-apps-v821-<board>.tar.gz` | rootfs overlay |
| `dist/aiglass-apps-<version>.tar.gz` | 源码发布包及配套 SHA-256 文件 |

rootfs overlay 的无线、MPP 和 RT Media 运行库由 BSP 提供。
`etc/media/v821-mode.sh` 提供角色/应用选择入口；固件启动服务须显式传入角色与已解析的模式，
板级启动服务和 JL 端协议由对应固件集成。

## 安装与发布

```bash
make -C Application install DESTDIR=/path/to/rootfs
make -C Application release
make -C Application clean
```

`install` 复用现有产物，源码变更后需先编译。固件集成时，在 BSP 生成 rootfs 后、
打包镜像前安装。源码包版本默认读取 `VERSION`，可通过 `AIGLASS_RELEASE_VERSION`
覆盖。

`clean` 删除编译目录，`distclean` 同时删除产物目录，`release-clean` 删除源码包。
完整命令见 `make -C Application help`。

## 相关文档

- [组件与依赖](MANIFEST.md)
- [BSP 集成](integration/BSP_DEPENDENCIES.md)
