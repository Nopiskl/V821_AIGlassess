# V821 本地 MPP 媒体接口

供常电 LVGL 宿主使用的 C11 媒体模块。接口为 `vm_media.h`；实现直接调用 V821 sun300iw1
Eyesee MPP，不依赖 AGLink、RT Media 或外部音频控制端。[运行模式](../RUNTIME_MODES.md)
说明它与 `aiglass-apps` 受控模式的关系。

## 参考与适配范围

接口依据为 V821 参考工程
`V821Vision_AMP/platform/allwinner/eyesee-mpp/middleware/sun300iw1/sample/` 中的示例。
V851 工程仅用于资源管理结构参考。

| 参考 | 本模块采用的调用方式 |
| --- | --- |
| `nopiskl_smartIPC_demo` | `VI_GetFrame → VENC_SendFrameSync → VENC_GetStream` JPEG 抓图；AI Get/Release 保存 WAV |
| `nopiskl_CDR_demo` | VI/VENC、AI/AENC、VENC/MUX、AENC/MUX 绑定；视频和音频编码配置 |
| V851 参考工程 `APP/Application` | 资源所有权、按初始化状态回收、临时文件完成后发布的组织方式 |

V821 接口调用包括 `AW_MPI_AI_CreateChn(dev, chn, &attr)`、
`AI_CHN_ATTR_S.nFrameSize`、`AIO_ATTR_S.mMicNum`、`VENC_SendFrameSync` 和
`VencEncppSharp_FollowISPConfig`。接口类型与 ABI 须使用目标 V821 BSP 的头文件和库验证。

## 能力与生命周期

| API | 行为 |
| --- | --- |
| `vm_media_open` | 校验配置、取得媒体锁；按配置初始化 MPP SYS |
| `vm_media_photo` | 启动本地 VI/ISP，JPEG 编码一帧，保存并释放采集资源 |
| `vm_media_record_start(..., VM_MEDIA_VIDEO, ...)` | H.264 MP4，可选本地 AI/AAC 音轨 |
| `vm_media_record_start(..., VM_MEDIA_AUDIO, ...)` | 后台线程采集本地 AI，保存 16-bit PCM WAV |
| `vm_media_status` | 查询模式、异步错误、停止标记和 WAV 有效数据字节数 |
| `vm_media_record_stop` | 停止输入、等待 WAV 线程、停止编码与封装、解绑并释放；完成文件 |
| `vm_media_close` | 回收剩余采集资源，按所有权退出 SYS，释放锁 |

同一实例的 API 必须由持有该实例的线程串行调用。一次只执行一个媒体任务，拍照也要求空闲。
MPP 回调只置错误状态，不操作 UI，不在回调内 Stop/Destroy。WAV GetFrame 超时按 100 ms
轮询，连续 5 秒没有帧报错；正常停止会实际关闭本地 AI 通道。

使用离线 VI/VENC 通路，初始化顺序参考 V821 示例。录像绑定完成后设置 H.264 SPS/PPS，
先启动接收端再打开输入。结束时先停输入，再停编码/MUX，解绑并销毁。初始化失败仅回收
已创建资源；Stop/UnBind 失败时保留依赖资源和回调 cookie，禁止新任务覆盖它们。
关闭函数在资源无法回收时返回错误并保留实例和锁；需由宿主处理或退出进程。

输出先写入同目录的 `.part`。成功停止并同步文件后用硬链接发布正式文件，不覆盖已有路径；
输出文件系统必须支持硬链接（参考固件的 UDISK 为 ext4）。录像/录音失败时保留 `.part`
供检查，不将其标记为成功文件；拍照失败会移除未完成文件。WAV 遵守 RIFF 32-bit 长度上限，
达到限制会停止；没有实现自动录像分段或文件修复。

## 配置

调用 `vm_media_config_init()` 后设置板级参数，再调用 `vm_media_open()`。默认通道配置未经
自制硬件验证，部署时须按板级路由调整。麦克风连接尚未确定，`ai_device` 和 `ai_channel`
保留为可配置参数，以最终 codec/I2S 路由为准。

| 字段 | 默认值 |
| --- | --- |
| `vipp / vi_channel / isp` | `0 / 0 / 0` |
| `venc_channel / mux_channel / aenc_channel` | `0 / 0 / 0` |
| `width / height / fps` | `1920 / 1080 / 30`；宽为 16 的倍数，高为偶数 |
| `video_bitrate` | `8000000` bit/s，H.264 CBR |
| `photo_quality` | `85`，范围 1..100 |
| `ai_device / ai_channel` | `0 / 0` |
| `sample_rate` | `16000`；支持配置 8000/16000/32000/44100/48000 |
| `audio_channels` | `1`；可配置 1 或 2，固定 16-bit PCM |
| `video_audio` | `true`；设为 false 生成无音轨 MP4，不影响独立录音 |
| `manage_system` | `true`；若宿主已经初始化 MPP SYS，则设为 false |

所有模式都保留 LVGL 宿主的显示生命周期；媒体操作不初始化/关闭 VO，不发送关机请求。
`manage_system=false` 时，宿主须在媒体服务关闭之前保持 SYS 有效。

LVGL 的环境变量映射见 [界面配置](../lvgl-home/README.md)。本模块不提供音频播放、输出音量
控制或实时取景。采集设备不可用时返回错误，音频来源固定为配置的 V821 本地 AI 通道。

## 构建接入

在完整 V821 BSP 中准备匹配的 C 编译器、归档工具、内核 user headers 和 MPP staging：

```sh
make -C Application local-media \
    CC=/path/to/riscv32-linux-musl-gcc AR=/path/to/riscv32-linux-musl-ar \
    V821_STAGING=/path/to/v821/staging_dir/target \
    V821_KERNEL_HEADERS=/path/to/v821/kernel/build/user_headers
```

产物为 `output/libv821_mpp_media.a` 与 `output/include/vm_media.h`。构建入口只生成开发库，
不构建 LVGL 宿主。`MPP_CFLAGS` 用于补充匹配 V821 CDR/IPC 目标的宏和头文件路径。
默认 `make` 显示构建用法。

宿主按“宿主对象 → 本归档 → V821 MPP 依赖”的顺序链接。BSP 可能选择静态或动态 MPP，
应采用其 CDR/IPC 目标中 VI/ISP/VENC/AI/AENC/MUX、media_utils、ion、音频编码库的链接参数，
并加入 pthread；RISC-V32 的 64-bit 原子操作可能还需要工具链 `libatomic`。不复用原
`aiglass-media.ldflags`，其中含受控模式的 AGLink/RT Media 依赖。

## 验证范围

静态验证范围包括 C 语法树、Cppcheck 及 V821 示例接口对照；LVGL 调用层使用依赖头文件进行
`-fsyntax-only` 检查。参考工程只包含 sample overlay，缺少完整 MPP 开发头文件和库。
本模块的完整类型检查、交叉编译、链接及板端运行未验证。

目标端验收须覆盖 Sensor/ISP/VIPP 映射、本地 AI 路由、JPEG 出图、MP4 音视频同步、反复启停、
磁盘满与断流处理、`.part` 发布，以及 MPP SYS 与显示宿主的资源所有权。
