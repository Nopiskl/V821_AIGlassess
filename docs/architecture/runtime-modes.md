# V821 运行模式

V821 设备支持两种互斥的运行模式，由启动服务选择角色。切换模式前须正常退出原进程，
并确认固件的媒体资源配置与角色匹配。模式切换不支持进程内热切换；板级 DTS 由固件配置管理。

Linux 主机模拟器是常电 UI 模式的开发环境，复用界面和业务服务，以软件显示和输入进行联调。
它不启动 V821 固件、MPP 或受控应用，不构成第三种设备部署模式。设计见
[Linux 主机模拟器架构](host-simulator.md)，使用方法见 [主机联调说明](../../Application/host-debug/README.md)。

| 项目 | 常电 UI 模式 | JL 主控受控模式 |
| --- | --- | --- |
| 控制与显示 | V821 运行 LVGL，持续显示 | JL 发起操作；V821 不启动 LVGL |
| 应用 | `lvgl-home` + `mpp-media`，接入已有显示宿主 | `aiglass-apps` 中的独立应用 |
| 拍照 | MPP VI → VENC JPEG | E907/RT Media fastboot 拍照；Linux photo 应用负责收尾 |
| 录像 | MPP VI → VENC → MUX | RT Media → MPP 管线 |
| 音频 | 可配置的 V821 本地 MPP AI | AGLink 接收控制端音频 |
| 停止录音 | 停止并释放本地 AI 通道 | 接收控制端 `AG_AD_RECORD_AUDIO_STOP` |
| 电源策略 | 媒体操作不发送关机消息 | 使用 `AG_VD_POWER_OFF` 协作策略 |
| HTTP AI | 由 LVGL 服务层调用 | 不由这些独立媒体应用实现 |

## 常电 UI 数据路径

```text
LVGL 操作 → 服务工作线程 → vm_media API → V821 Eyesee MPP
                                     ├─ VI → JPEG VENC → JPEG 文件
                                     ├─ VI → H.264 VENC ─┐
                                     │  AI → AAC AENC ──┴→ MUX → MP4
                                     └─ AI → PCM WAV → 录音文件 / ASR
```

媒体代码不创建 AGLink 通道，不依赖 JL 发送音频，不调用 RT Media、ALSA 用户态 API、
V4L2 ioctl 或 FFmpeg。

MPP SYS 在首次媒体操作时初始化并保留到服务关闭。摄像头和采集通道按任务启停；常电指
界面常驻，不表示摄像头、麦克风一直采集。若显示宿主已经通过 MPP VO 管理 SYS，设置
`manage_system=false`，并由宿主在媒体服务前初始化 SYS、在媒体服务之后退出 SYS。

接口与配置见 [本地 MPP 后端](../../Application/mpp-media/README.md)。相机页提供控制和状态，不含实时
取景；音频播放与输出音量控制未接入。媒体与 HTTP 共用服务控制线程，长任务会影响
控制响应；MPP 数据处理和 WAV 采集在内部线程中进行。

## 受控模式传输路径

fastboot 参考配置使用以下传输路径：

```text
外部控制端（JL）
    ↕ UART2（该参考配置）
V821 E907：aglink_rpbuff_uart_init(CONFIG_RPBUFF_UART_PORT)
    ↕ RPBuf
V821 Linux：rpbuf-aglink 驱动 ↔ Netlink ↔ libaglink ↔ ai_glass_*
```

配置定义位于 `SDK/TinaLinux 5.0/rtos/lichee/rtos/projects/v821_e907/aiglass_fastboot/`
下的 `src/main.c`、`defconfig`，以及 fastboot Linux `board.dts` 中的 `rpbuf_aglink`。
其中 `CONFIG_COMPONENTS_RPBUF_UART=y`、`CONFIG_RPBUFF_UART_PORT=2`；RTOS 配置的
UART2 引脚是 PL04/PL05。自制 PCB 的 UART 路由须依据原理图和板级配置另行核对。

该路径用于 JL 主控、V821 执行媒体任务的受控模式。AGLink 协议本身不限制显示状态，
本项目在受控模式下停用 LVGL。Linux 应用不额外打开 `/dev/ttyS2`，也不与
E907 竞争同一 UART。JL 端协议实现、波特率和板级电气连接需结合硬件另行适配。

## V821 启动入口

`aiglass-apps` 打包时安装 `/etc/media/v821-mode.sh`。以下为目标设备的部署用法：

```sh
# 常电模式：路径须指向已有的、集成 lvgl-home 的宿主程序。
/etc/media/v821-mode.sh ui /usr/bin/glasses-ui

# 受控模式：由 BSP 启动服务传入已选定的 AG_MODE 或名称。
/etc/media/v821-mode.sh controlled video
/etc/media/v821-mode.sh controlled 6  # 独立录音
```

脚本只执行白名单应用，使用 `exec` 转交信号；不自动启动两种角色，不杀死现有进程。
其 mode 参数由板级启动服务根据 E907/boot flow 的模式选择结果提供。启动模式读取属于
BSP 集成职责，脚本不解析 RTC/sysfs 节点或 UART 数据。

LVGL 服务和七个独立应用入口共同持有 `/tmp/v821-mode-owner.lock`；本地 MPP 后端、
独立应用及受控媒体 API 使用 `/tmp/v821-media-owner.lock` 保护媒体资源。竞争启动会返回
错误。不使用这些锁的旧版应用不受互斥机制约束，部署时须关闭其自启动服务。

常电固件必须将摄像头/ISP/编码器的所有权交给 Linux MPP，不能同时让 E907 fastboot/RT Media
预启动并持有相同通道。受控模式则沿用对应的 E907、RPBuf、RT Media 配置。仅切换 Linux
可执行文件不足以解决两套固件对媒体硬件的争用。

## 构建与验证

- `make -C Application local-media ...`：生成常电模式 `libv821_mpp_media.a`。
- `make -C Application all ...`：七个受控应用及可选 `libaiglass_media.a`。
- LVGL 宿主只链接本地 MPP 库，不链接受控媒体库；两套 API 不在同一进程中同时打开。
- V821 媒体实现与受控应用的验证范围限于静态检查；目标端编译、链接与运行未验证。目标验证
  需要完整 V821 BSP 的 MPP 头文件、库及硬件；V851 参考工程不能替代 V821 ABI 验证。
- Linux 主机有独立的原生构建和集成测试入口，覆盖共用 UI、配对、文件及 HTTP 服务；
  主机结果不能替代上述目标端验证。

受控命令、方向、载荷及完成时序见 [AGLink 协议说明](../../Application/aiglass-apps/integration/AGLINK_PROTOCOL.md)。
