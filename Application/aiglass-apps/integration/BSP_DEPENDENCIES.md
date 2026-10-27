# BSP 依赖边界

独立构建入口以本补丁包的 `apps/` 为主程序源码，不读取 BSP 中的
`platform/allwinner/eyesee-apps/demo_ai_glass`，也不通过软链接引用 BSP 的
`device/`、`platform/` 或 `openwrt/package/` 目录。

编译时仅从 `AIGLASS_BSP_ROOT` 使用以下 BSP 产物：

- `out/v821/<board>/openwrt/staging_dir/toolchain`：RISC-V musl 交叉工具链；
- `out/v821/<board>/openwrt/staging_dir/target`：无线、RT Media、Eyesee MPP、
  RTSP、PDET 等头文件和库；
- `out/v821/kernel/build/user_headers`：AGLink 等内核 UAPI。

本仓库统一入口 `make -C Application ...` 默认 `<board>=aiglass_fastboot`。
`SDK/TinaLinux 5.0` 只提供板级补丁，无法单独生成这些产物；需要通过
`AIGLASS_BSP_ROOT` 指向完整的 Tina V821 BSP。

`make bsp-prepare` 会构建上述依赖，但不会构建 BSP 内的 `demo_ai_glass`。
BSP 已具备上述产物时，执行 `make doctor` 检查后即可编译。

`scripts/bsp-package.sh` 的依赖包含 wirelesscommon、wifimanager、libawion、
libcedarc、person_detection、motion_detection、RT Media、Eyesee MPP 和 RTSP，
对应应用 Makefile 中引用的无线、编解码、检测及媒体库。

## 与固件构建集成

独立构建生成的 overlay 可通过 `make install DESTDIR=/absolute/path/to/rootfs`
安装到已展开的目标 rootfs，然后由完整 BSP 打包镜像。该步骤应在 BSP 完成 rootfs
生成之后执行，避免后续 BSP 包安装覆盖应用产物。

若要沿用完整 BSP 自带的 `ai-glass` OpenWrt 包构建链，需要将本目录的 `apps/`
内容复制到 BSP 的 `platform/allwinner/eyesee-apps/demo_ai_glass/`。在仓库根目录
执行，`AIGLASS_BSP_ROOT` 为已应用板级补丁的完整 BSP：

```bash
export AIGLASS_BSP_ROOT=/absolute/path/to/tina-v821-release
test -f "$AIGLASS_BSP_ROOT/build/envsetup.sh" && \
    test -d "$AIGLASS_BSP_ROOT/platform/allwinner/eyesee-apps/demo_ai_glass" && \
    cp -a Application/aiglass-apps/apps/. \
        "$AIGLASS_BSP_ROOT/platform/allwinner/eyesee-apps/demo_ai_glass/"
```

复制会覆盖 BSP 中的同名源码，应先保存 BSP 侧已有的修改。随后按 BSP 原生流程
加载环境、`lunch v821-aiglass_fastboot-tina`，清理并重建其 `ai-glass` 包及固件。
本仓库的 fastboot `defconfig` 已选中 `ai-glass`、`ai-glass-lib` 和全部 7 个子应用，
不需要另外新增一套同名 OpenWrt 包。

两种方式的构建入口各自独立：仅修改本目录的源码不会自动更新 BSP 中已复制的
副本。仓库不包含完整 BSP 的原生包规则，原生包编译和整机功能需要在
完整 BSP 与目标板上验证。

## 运行时依赖

受控模式可选 `media` 开发库沿用 video 的 RT Media、MPP 与 AGLink 依赖。
该目标不构建界面程序，接口与链接方法见 [受控媒体接口](CONTROLLED_MEDIA.md)。

二进制 `make package` 打包本工程生成的 `ai_glass_*`、`libaglink.so`，以及
`log_handle.sh`、`rtc_init.sh`、`v821-mode.sh`。其他运行库由 BSP 固件/OpenWrt 包提供。

BSP 的 `wirelesscommon`、`wifimanager` 等依赖主要以 `.so` 形式提供。
源码发布包禁止目录软链接；ELF 的动态链接依赖由目标固件提供。
全静态链接需要 BSP 提供每个依赖及其传递依赖的静态库，并重新核对许可证和链接顺序。
