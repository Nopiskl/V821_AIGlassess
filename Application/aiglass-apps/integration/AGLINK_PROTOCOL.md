# AGLink：V821 受控模式协议与接入

本文说明 V821 受控应用使用的 AGLink 消息、状态流程及 JL 固件接入边界。
本仓库不包含 JL 端协议实现；常电 LVGL 使用独立本地 MPP 接口。

协议依据为 V821 BSP `bsp/include/uapi/aglink/ag_proto.h`，版本宏
`AGLINK_VERSION="1.0.0.1.3.2505301030"`，以及本包的 `ag_wrap.h`、七个应用与 `sys_info.c`。
目标固件须使用对应版本的 UAPI 复核消息编号、结构布局和应用支持范围。

## 1. 传输与职责

仓库 fastboot 参考配置的路径为：外部控制端 UART2 ↔ V821 E907 ↔ RPBuf ↔ Linux
AGLink 驱动 ↔ Netlink（用户态头文件定义协议号 29）↔ libaglink ↔ 应用。
E907 程序调用 `aglink_rpbuff_uart_init(CONFIG_RPBUFF_UART_PORT)`；defconfig 设置端口 2。
Linux 应用不直接读写 UART。`aiglass` 与 `aiglass_fastboot` 的引脚配置不能混用。

JL 端负责电源与启动协调、模式选择、协议收发和相应模式的音频数据。Linux 侧使用
BSP AGLink 库及 E907/RPBuf 路径，串口组帧和转发由 BSP 处理。

协议消息可经 E907/内核处理或转发，并非每个 AD 消息都由 Linux 应用直接接收；
`AG_AD_DIS_LINE` 等事件也不能假定是 JL 应直接发送的串口帧。

## 2. UAPI 布局与串口验证边界

参考 RTOS 配置为 `checksum=0`。未启用 `AGLINK_HDR_CKSUM` 时，UAPI 布局如下：

| 偏移 | 类型 / 字节 | 字段 |
| --- | --- | --- |
| 0 | uint16 / 2 | `tag`；协议定义标识字符串 `AW` |
| 2 | uint8 / 1 | `type`；按方向使用 AD 或 VD 枚举 |
| 3 | uint8 / 1 | `ch` |
| 4 | uint16 / 2 | `seq` |
| 6 | uint16 / 2 | `len`，payload 字节数 |
| 8 | byte[] | `payload` |

这是 packed UAPI 结构在目标小端平台的内存布局。消息方向是解码条件，同一个数值在 AD
和 VD 中含义不同。多字节字段与未对齐字段应逐字节读写，不直接强转 JL 的自然对齐结构。

本包提供预编译 `libaglink.a`，不包含完整包装层、E907 串口组帧与流控源码。
上述布局仅定义 UAPI 内存结构；`ch` 的串口取值、序号与确认规则、转义、分片、重传、握手和
波特率须依据 BSP 实现或抓包结果确认。启用 checksum 后头布局改变，CRC 算法和覆盖范围
须与对端实现一致。UART 线协议的互通验证不在静态结构检查范围内。

`protocol_contract.c` 用真实 BSP 头文件的静态断言检查模式/消息编号与关键结构大小：

```sh
cc -std=c11 -fsyntax-only -I /path/to/bsp/user_headers/include/bsp \
    Application/aiglass-apps/integration/protocol_contract.c
```

## 3. 模式选择与 V821 应用

初始模式通过 `AG_AD_TIME` 的 `ag_time.mode` 进入原启动流程。仓库 E907 示例将模式写入
RTC 保留字段；板级启动服务负责将已解析模式交给 Linux 应用启动入口。

| AG_MODE 数值 | 名称 | V821 应用 |
| --- | --- | --- |
| 0 | PHOTO | `ai_glass_photo`；依赖 fastboot 拍照已执行 |
| 1 | VIDEO | `ai_glass_video` |
| 2 | DOWNLOAD | `ai_glass_download` |
| 3 | OTA | `ai_glass_ota` |
| 4 | AI | 七个独立应用中无对应入口 |
| 5 | NORMAL | `ai_glass_normal` |
| 6 | RECORD_AUDIO | `ai_glass_audio` |
| 7 | IDLE | 不启动媒体任务 |
| 8 | LIVESTREAM | `ai_glass_livestream` |

V821 端提供 `/etc/media/v821-mode.sh controlled <名称或数值>` 白名单入口。未知/不支持的
模式返回错误。脚本接收已解析的模式参数，不解析 UART 数据或读取 RTC 节点。
Linux 启动服务的模式读取与应用调度由对应 BSP 实现。

`struct ag_time` 为 packed/aligned(2)，大小 14 字节：mode(uint8)、timestamp(uint64)、
timeout(uint8)、width(uint16)、height(uint16)，偏移分别为 0/1/9/10/12。
width/height 在录像应用中表示缩略图尺寸。timestamp 的单位及 timeout 的流控语义未确认，
须依据对应 E907 实现核实。

## 4. 媒体消息

AD 表示进入 V821 处理路径的方向；VD 表示 V821 向控制端发送的方向。

| 方向 | ID | 消息 | 载荷与应用行为 |
| --- | --- | --- | --- |
| AD | 1 | TIME | `ag_time`，启动模式及相关参数 |
| AD | 5 | AUDIO | 音频载荷；录像模式为原应用约定的 PCM，独立录音原样保存 |
| AD | 7 | VIDEO_STOP | 无业务载荷；结束当前录像 |
| AD | 8 | RECORD_AUDIO_STOP | 无业务载荷；同步关闭录音文件，更新原媒体索引 |
| AD | 10 | TAKE_SG_PHOTO | UAPI 有定义；本包 Linux photo 应用未实现该主动拍照处理 |
| AD | 16 | SAVE_SYS_LOG | 原 photo 应用据此进入拍照完成收尾；不是通用拍照请求 |
| VD | 1 / 2 | SYNC_1 / SYNC_2 | `ag_sync`：mode + 5 字节 str；normal 等应用发送 `U_OK` 的 SYNC_2 |
| VD | 3 / 4 | THUMB / THUMB_END | 缩略图数据及结束通知；分片规则需匹配底层库 |
| VD | 5 | POWER_OFF | 原应用完成/失败后的电源协作通知；不能单独代表操作成功 |
| VD | 8 | VIDEO_START | 本地录像初始化后通知控制端 |
| VD | 9 | VIDEO_END | UAPI 定义；原独立 video 成功路径使用 THUMB_END + POWER_OFF |
| VD | 16 | RECORD_AUDIO_START | 本地录音应用准备好后通知控制端发送数据 |
| VD | 17 | SG_PHOTO_END | UAPI 定义；本包 Linux photo 应用未实现会话内单拍处理 |
| VD | 21 | GENERIC_RESPONSE | `ag_generic_rsp`：mode(uint8)、code(uint8) |

原录像音频配置为单声道、16 kHz、16-bit PCM，送入 MPP AAC 编码。音频必须按样本边界
发送，参数变更需要两端同时调整。其裸数据率为 32000 byte/s；8N1 下仅载荷就需要约
320 kbit/s，115200 baud 不足。实际波特率仍须服从 BSP，不能只按这项带宽计算设置。

原独立录音将 `AG_AD_AUDIO` 载荷按顺序写入 `.opus`，不补充 Ogg 封装，不实施 Opus 编码。
控制端须确定实际编码/分包；`.opus` 扩展名本身不构成容器约定。UAPI 有 1024 字节音频结构，
实际分包上限和库的分片行为须在对应固件核对，不能依据 tx 的 uint16 长度就发送任意大包。

## 5. 操作时序

### 录像

1. JL/启动流程选择 VIDEO，V821 启动 `ai_glass_video`。
2. V821 初始化 RT Media、AAC、MP4 管线，发送 `AG_VD_VIDEO_START`。
3. 控制端持续发送 PCM `AG_AD_AUDIO`；视频来自 V821 摄像头。
4. 控制端发 `AG_AD_VIDEO_STOP`，并结束音频发送。
5. V821 完成封装和文件同步，处理索引/陀螺仪文件，发送缩略图及 `THUMB_END`，再发
   `POWER_OFF`。失败路径先发 `GENERIC_RESPONSE`，再发 `POWER_OFF`。

录像应用在初始化阶段收到停止请求、连接断开或进程退出时释放资源；AP/STA 配置消息与 PCM
音频分别处理。控制端须等待 `VIDEO_START` 后发送音频，发送停止请求后仍需等待完成通知。

### 独立录音

1. 选择 RECORD_AUDIO，启动 `ai_glass_audio`。
2. 收到 `RECORD_AUDIO_START` 后发送约定格式的 `AG_AD_AUDIO`。
3. 控制端结束音频发送，并发送 `AG_AD_RECORD_AUDIO_STOP`。
4. V821 同步关闭文件、更新索引；成功路径发 `POWER_OFF`，失败路径先发错误响应。

UAPI 未定义 `AG_VD_RECORD_AUDIO_STOP`。JL 端负责自身音频采集的启动和停止，V821 文件
关闭不表示控制端麦克风关闭。常电 UI 的本地 AI 录音使用独立的采集生命周期。

### fastboot 拍照

模式选择和拍照在原 E907/RT Media 启动流程完成；Linux `ai_glass_photo` 等待
`AG_AD_SAVE_SYS_LOG` 后发送 `POWER_OFF`。它不提供“Linux 已启动后反复发送拍照请求”的
通用服务。会话内连续单拍需要基于可选受控媒体 C API 实现控制服务；Linux photo 应用
不处理 `TAKE_SG_PHOTO` 主动拍照请求。

## 6. 系统信息与错误

独立应用调用 `aglink_info_app_init()` 注册系统信息处理：版本、温度、存储、媒体数量及
删除等请求由 `apps/src/misc/sys_info.c` 处理。Wi-Fi、OTA、下载及直播另由各自应用处理。
AGLink 应用回调由单个独立应用持有，同一设备只允许启动一个受控应用。

常见通用 code：0 SUCCESS、1 REJECTED、2 NOMEMORY、3 WRITE_FILE、4 READ_FILE、
5 PROCESS、6 NETWORK、7 PARAMETER、15 DISK_FULL、16 MOUNT_DISK、17 UNKNOWN。
code 的完整定义以 BSP UAPI 为准；`POWER_OFF` 不是成功响应。

若控制端未收到完成通知，需按已确认的超时/恢复策略处理。强制断电可能留下未完成文件，
应用不提供掉电恢复保证。JL 端须实现电源动作与完成确认策略。

## 7. 集成与验证范围

- V821 角色入口负责应用选择；七个应用通过运行模式锁实现互斥，并处理媒体停止和资源释放。
- 传输依赖 BSP 的预编译 AGLink 库、E907 桥接及板级 UART 配置。完整 UART 线协议须依据
  供应商实现或抓包结果核实；JL 端协议需要独立适配。
- BSP 启动服务和硬件资源分配由固件集成负责。常电与受控模式的配置要求见
  [运行模式](../../RUNTIME_MODES.md)。
- 可选 `libaiglass_media.a` 是进程内受控媒体 API，不是 UART 命令服务；其主动开始/停止调用
  约定见 [受控媒体接口](CONTROLLED_MEDIA.md)，与原七个应用的自动收尾策略不同。
- 验证范围限于静态代码与协议契约检查；目标端构建、串口互通、音频及摄像头运行未验证。
