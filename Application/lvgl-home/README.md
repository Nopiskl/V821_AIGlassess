# V821 眼镜界面

基于 LVGL 8.3/8.4、Linux 和 V821 本地 MPP 的常电眼镜界面模块，默认采用 `640 × 480` 横屏布局。
采用黑底、绿色线框图标、顶部状态栏、左侧应用导航和底部常驻消息区。
视觉布局参考 [光波导 AR 眼镜项目的“眼镜页面与功能”](https://oshwhub.com/sgsax/project_tiqnqiyt)，
界面与图标由 LVGL 对象和绘图接口绘制。

本模块接入已有 LVGL 显示程序，不包含显示驱动、LVGL 库或手机 APK，也不加入
`Application/Makefile` 中七个厂商应用的独立构建。媒体后端使用独立的 `Application/mpp-media` 开发库。

Linux 主机可通过 [host-debug](../host-debug/README.md) 独立编译运行本模块，并与 uni-app 手机端
联合调试。该构建提供软件显示和浏览器输入，复用本目录的 UI、服务和手机协议，禁用设备相关功能；
无需连接 V821。构建、依赖、运行数据与测试报告均保存在工作区。
源码复用方式、显示与输入适配、线程及通信关系见 [模拟器架构](../../docs/architecture/host-simulator.md)。

## 页面与功能

| 页面 | 功能 | 依赖与限制 |
| --- | --- | --- |
| 菜单 | 两行八个入口、全部应用、时钟、网络/手机/电池状态 | 网络状态读取网卡 IPv4 地址，电池读取 Linux power_supply |
| AI 助手 | 文本输入、多轮对话、语音提问、取消请求 | 配置 HTTP 对话接口；语音另需接受 PCM WAV 的 ASR 接口 |
| 实时翻译 | 中/英/日目标语言切换、文本翻译、语音翻译、连续字幕 | 连续字幕依次执行分段录音、ASR、翻译，存在段间间隔，不是流式同传 |
| 设备配对 | 首次启动显示六位配对码、保存绑定、解除配对、自动恢复连接 | 配对逻辑由主机和实机共用的 C 服务实现；真实网络配置另行接入 |
| 手机互联 | 心跳、通知、聊天/翻译草稿接收、文件收发和传输计数 | Unix socket 或可选 IPv4 TCP，协议见下文 |
| 导航、骑行 | 显示手机发送的路线文字、速度和里程 | 路线规划与定位由手机提供 |
| 录音机 | 开始/停止录音、计时、文件入口 | 本地 MPP AI 采集，保存 16-bit PCM WAV |
| 相机 | 拍照、开始/停止录像、文件入口 | 本地 MPP VI/VENC JPEG 抓图及 H.264/AAC MP4；通道与参数可配置，无实时取景 |
| 文件管理 | 收件箱/媒体/笔记目录、文本预览、确认删除 | 每目录最多列出 32 项；文本预览最多 8191 字节；不支持播放，播放按钮禁用 |
| 记事本 | 编辑、修改保存文件名、保存和恢复默认笔记 | UTF-8 文本，单条内容最多 2047 字节；修改文件名表示另存 |
| 文本浏览 | 输入 HTTP(S) 地址、提取网页文字、取消加载 | 不执行脚本、不加载图片、不实现网页排版；最多读取 256 KiB 响应 |
| 设置 | 亮度调节，延迟 300 ms 合并连续调整 | sysfs backlight；不提供输出音量与播放接口 |
| 应用 | 内置应用入口、HTTP(S) 文件下载 | 文件保存到收件箱，不安装或执行下载内容 |
| 小游戏 | 飞机、赛车、恐龙跑酷、靶场；碰撞/得分/暂停/重试 | 本地 LVGL 图形小游戏 |
| 专注面板 | 打坐计时、木鱼计数、保存和恢复进度 | 本地 `notes/focus.json`；不包含账号或在线养成系统 |

未配置服务或设备不可用时，界面显示对应错误；导航数据由手机提供，音频数据由采集设备提供。
屏幕键盘支持 Latin 输入；中文可通过手机草稿、语音识别或宿主输入法输入。
默认字体包含 ASCII、常用标点、假名和 CJK 基本区；未覆盖的字符使用 LVGL 默认字体回退。

## 接入已有 LVGL 程序

将以下源文件加入宿主工程，并添加本目录、LVGL、libcurl 和本地 mpp-media 的头文件搜索路径：

```text
aiglass_home.c
aiglass_game.c
aiglass_services.c
aiglass_http.c
aiglass_phone.c
aiglass_pairing.c
aiglass_media.c
fonts/aiglass_font_16.c
third_party/cJSON.c
```

依赖 C11、POSIX threads、Linux 头文件和 libcurl（至少 7.56，需 HTTP/HTTPS 与 MIME 支持）。
链接宿主 LVGL、`libv821_mpp_media.a`、libcurl、POSIX threads，以及目标 V821 BSP 的
VI/ISP/VENC/AI/AENC/MUX 等依赖。模块包含 cJSON 1.7.19；宿主若已有 cJSON，应统一版本。
本地媒体库不链接 AGLink 或 RT Media，用户态不直接使用 ALSA、V4L2 ioctl 或 FFmpeg。

媒体库入口为 `make -C Application local-media`，配置和依赖见
[本地 MPP 接口](../mpp-media/README.md)。这是常电 UI 模式；与独立应用的关系见
[运行模式](../../docs/architecture/runtime-modes.md)。MPP 构建需要完整 V821 BSP，目标编译与链接未验证。
主机构建使用独立入口和返回 `ENOTSUP` 的媒体替代实现。

宿主 `lv_conf.h` 至少启用：

```c
#define LV_USE_USER_DATA 1
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_SLIDER 1
#define LV_TXT_ENC LV_TXT_ENC_UTF8
```

保持一个可用的 `LV_FONT_DEFAULT`，用于符号与未覆盖字符回退。内置字体位图约 671 KiB，
字形描述和映射表另占只读存储。字体不计入 LVGL 动态内存池；UI、消息队列与 HTTP 缓冲区
仍需预留内存。使用 LVGL 内置分配器时，可从 256 KiB 内存池开始评估，实际峰值需板端测量。

初始化显示和 LVGL tick 后，在 LVGL 所在线程创建界面：

```c
#include "aiglass_home.h"

ag_services_config_t linux_config;
ag_services_config_init(&linux_config); /* 读取 AIGLASS_* 环境变量 */

aiglass_home_config_t ui_config = {
    .width = 640,
    .height = 480,
    .services_config = &linux_config,
    .input_device = "/dev/input/event0"  /* 可省略，沿用宿主输入设备 */
};
lv_obj_t *view = aiglass_home_create_with_config(lv_scr_act(), &ui_config);
/* 宿主继续维护 lv_tick_inc() 和 lv_timer_handler()。 */
```

宽度范围为 `640..2048`，高度范围为 `480..2048`，默认 `640 × 480`，显示设备应能容纳该尺寸。布局尺寸不改变 framebuffer 的分辨率
或旋转方向；这些由宿主显示驱动配置。自定义字体的行高须在 `1..22` 范围内。
简化入口 `aiglass_home_create(parent)` 和 `aiglass_home_show()` 默认使用 `640 × 480`。
`show()` 返回新建的 screen；页面/按键 API 应接收 `create*()` 返回的 view。

所有 UI API 在 LVGL 线程调用。`aiglass_home_open(view, page)` 切换页面，
`aiglass_home_key(view, LV_KEY_...)` 接收方向、确认、返回及焦点切换按键。
也可使用 `lv_indev_set_group(indev, aiglass_home_group(view))` 接入宿主 keypad/encoder；
统一的返回和游戏快捷键建议经过 `aiglass_home_key()` 转发。
同一个 evdev 设备只能由宿主驱动或本模块中的一方读取。

内置 evdev 映射：方向键控制焦点/滚动/游戏，Enter/OK 确认，Esc/Back 返回，
音量加/减切换前后焦点，Tab 切换下一焦点。触摸操作通过 LVGL 原有输入设备处理。

`lv_obj_del(view)` 释放界面、定时器和输入设备。默认由 view 创建并持有 Linux 服务，删除时
取消请求、停止媒体采集并等待线程结束；因此销毁可能等待厂商驱动返回或网络取消。
若传入 `.services`，服务由宿主持有，宿主应在 view 删除后调用 `ag_services_close()`。
每个服务实例只能有一个结果消费者，不支持多个 view 共用同一实例。

## Linux 配置

环境变量由 `ag_services_config_init()` 读取，也可在调用后修改结构体中的同名字段。
字符串字段必须以 NUL 结尾。界面初始化时创建存储目录与通信 socket。`hardware_access`
默认开启，服务取得运行模式锁，并在首次媒体操作时初始化本地 MPP。媒体采集和 AI 请求由用户操作触发。
主机入口设置 `hardware_access=false`，禁用模式锁、背光、电池与媒体硬件访问，并链接返回
`ENOTSUP` 的媒体实现。UI、配对、文件和 HTTP 源码不使用主机专用条件编译。

| 环境变量 | 默认值 / 用途 |
| --- | --- |
| `AIGLASS_DATA_DIR` | `./aiglass-data`；自动建立 notes、media、inbox，父目录须已存在 |
| `AIGLASS_SOCKET` | 数据目录下的 `bridge.sock`；完整路径须短于 108 字节 |
| `AIGLASS_CHAT_URL` | 空；完整对话接口 URL |
| `AIGLASS_ASR_URL` | 空；完整语音识别接口 URL |
| `AIGLASS_API_KEY` | 空；两类接口共用的 Bearer token，无鉴权服务可留空 |
| `AIGLASS_MODEL` / `AIGLASS_ASR_MODEL` | 空；非空时发送对应 model 字段 |
| `AIGLASS_SPEECH_SECONDS` | `4`；每次语音采集时长，范围 1..15 秒 |
| `AIGLASS_VIPP` / `AIGLASS_VI_CHANNEL` / `AIGLASS_ISP` | `0 / 0 / 0`；按 V821 Sensor/ISP/VIPP 路由配置 |
| `AIGLASS_VENC_CHANNEL` / `AIGLASS_MUX_CHANNEL` | `0 / 0` |
| `AIGLASS_CAPTURE_WIDTH` / `AIGLASS_CAPTURE_HEIGHT` / `AIGLASS_CAPTURE_FPS` | `1920 / 1080 / 30`；宽须为 16 的倍数，高须为偶数 |
| `AIGLASS_VIDEO_BITRATE` | `8000000` bit/s |
| `AIGLASS_PHOTO_QUALITY` | `85`；JPEG 质量，范围 1..100 |
| `AIGLASS_AI_DEVICE` / `AIGLASS_AI_CHANNEL` / `AIGLASS_AENC_CHANNEL` | `0 / 0 / 0`；麦克风硬件未定，需按实际路由设置 |
| `AIGLASS_AUDIO_RATE` / `AIGLASS_AUDIO_CHANNELS` | `16000 / 1`；固定 16-bit，支持 1/2 通道 |
| `AIGLASS_VIDEO_AUDIO` | `1`；设为 0 生成无音轨 MP4 |
| `AIGLASS_MPP_MANAGE_SYS` | `1`；若宿主已初始化 MPP SYS，设为 0 并由宿主管理其生命周期 |
| `AIGLASS_ASR_MIME` | `audio/wav`；上传本地 PCM WAV，修改 MIME 不进行格式转换 |
| `AIGLASS_BACKLIGHT` | 空时查找 `/sys/class/backlight/*/brightness`，也可指定完整节点路径 |
| `AIGLASS_INPUT_DEVICE` | 空；可选 Linux evdev 路径 |
| `AIGLASS_PHONE_PORT` | `0`，关闭 TCP |
| `AIGLASS_PHONE_BIND` | `127.0.0.1`；开启 TCP 时的监听 IPv4 地址 |
| `AIGLASS_PHONE_TOKEN` | 空；启用 TCP 时必须至少 16 字节，每条请求携带 |

数据目录须归当前进程用户所有，且不可被组或其他用户写入。文件访问限制在 notes、media、inbox
三个平面目录内，不跟随符号链接。上传和下载文件上限均为 32 MiB；新下载/上传不覆盖同名文件。
共用服务不自动清理异常退出遗留的 socket，清理前须确认原进程已退出。
主机入口持有运行目录锁后清理该实例的遗留 socket，具体顺序见 [模拟器生命周期](../../docs/architecture/host-simulator.md#4-构建与进程生命周期)。

媒体操作统一调用 `vm_media`：录像使用本地 VI/VENC 和可选 AI/AENC，拍照使用 JPEG VENC，
录音使用本地 AI。停止会结束本地采集，无需等待 JL 命令。摄像头和麦克风按需启停，
MPP SYS 保留至媒体服务关闭；常电模式不发送关机消息。

拍照、录音、录像和语音采集共用一个媒体任务槽；与受控模式的 `ai_glass_*` 应用互斥。
照片/录像/录音保存在 media 目录，不使用旧应用的 `media.config` 或陀螺仪文件。
文件先写 `.part`，成功后发布为正式名称；失败时返回错误。
MPP 抓图及初始化/停止为同步接口，取消请求在厂商调用返回后生效。

## HTTP 服务约定

对话和文本翻译使用 `POST AIGLASS_CHAT_URL`，请求头为 `Content-Type: application/json`。
配置 API key 时附带 `Authorization: Bearer ...`。请求形态如下：

```json
{"model":"your-model","stream":false,"messages":[{"role":"system","content":"..."},{"role":"user","content":"你好"}]}
```

`model` 未配置时省略。普通对话保留最近四轮成功对话；翻译使用独立指令，不加入对话历史。
支持以下任一响应形态：

```json
{"text":"示例响应"}
```

```json
{"choices":[{"message":{"content":"示例响应"}}]}
```

语音先通过本地 MPP AI 采集一个 WAV 片段，再 `POST AIGLASS_ASR_URL`，使用 multipart/form-data，文件字段为 `file`，
可选字段为 `model`。响应为 `{"text":"识别结果"}`，随后将识别文本提交对话或翻译接口。
临时语音文件在成功、失败或取消后删除。连续字幕重复此流程；识别结果与译文均显示在界面中。
音频由本地 AI 输出 16-bit PCM，代码写入 RIFF/WAVE 头；ASR 网关须接受配置采样率/通道数
的 WAV。语音采集不依赖控制端的 Opus 编码、封装或停止消息。

HTTP 连接超时 5 秒、单次请求总超时 30 秒，响应上限 256 KiB。仅接受 HTTP(S)，
不跟随重定向；HTTPS 使用 libcurl 的 CA 配置验证证书。下载和网页请求不携带 AI API key。
网络与媒体操作在工作线程完成，LVGL 定时器取回结果，工作线程不调用 LVGL。

## 手机通信协议 v2

Unix socket 使用同 UID 凭据校验与 `0600` 权限。TCP 需显式开启并配置 token；
TCP 传输本身不加密，适用于受信局域网或 USB/SSH 隧道，跨网部署应由网关提供 TLS。
uni-app H5 通过工作区 HTTP 桥接访问该 Linux 协议服务；浏览器不直接打开 Unix/TCP socket。

每条消息为 UTF-8 JSON 加换行，单行小于 16384 字节，最多同时连接四个客户端。
请求可带 `id`，响应原样回传；除 `hello` 与 `pair` 外，每条请求必须带配对返回的 `credential`。TCP 每条请求另带 `token`。成功响应含 `"ok":true`，
失败响应含 `"ok":false,"code":负errno,"error":"原因"`。
建议每 5 秒发送 `ping`；15 秒无成功请求显示断连，30 秒无数据关闭连接。

| op | 字段 | 行为 / 响应字段 |
| --- | --- | --- |
| `hello` / `ping` / `status` | `hello` 无需配对凭据；其余带 `credential` | `device`、`device_id`、`paired`、`version:2`、`battery`、`runtime`、`address`、`bytes_received`、`bytes_sent`、`navigation`、`capabilities`；未知电量为 -1 |
| `pair` | `code`：屏幕显示的六位数字字符串 | 返回 64 个十六进制字符组成的 `credential`，保存绑定；已绑定时拒绝再次配对 |
| `unpair` | `credential` | 撤销绑定并生成新配对码；原凭据立即失效 |
| `notify` | `text` | 在手机互联页及底部消息区显示内容，text 少于 2048 字节 |
| `input` | `target:"chat"` 或 `"translate"`、`text` | 填入草稿并切换页面，由用户在眼镜端确认发送 |
| `navigation` | `text`、`speed_kmh`、`distance_km` | 更新导航/骑行数据，text 少于 256 字节 |
| `list` | `folder:"inbox"` / `"media"` / `"notes"` | 返回 `files:[{"name":"...","size":123}]`，最多 32 项 |
| `put` | `name`、`offset`、`data_hex`、`final` | 向 inbox 写入一块；响应下一个 `offset` 和 `final` |
| `get` | `path:"folder/name"`、`offset` | 返回 `data_hex`、下一个 `offset` 和 `final` |
| `note` | `name`、`text` | 原子创建 notes 下的 UTF-8 笔记，不覆盖同名文件；返回 `path`，内容最多 2047 字节 |
| `rename` | `path:"folder/name"`、`name` | 在原目录内改名，不覆盖同名文件 |
| `delete` | `path:"folder/name"` | 删除三个允许目录内的普通文件，不跟随符号链接 |
| `abort_put` | `name` | 删除 inbox 中对应的未完成上传；不删除已发布文件，临时文件不存在时仍成功 |

`runtime` 为 `linux-host` 或 `v821`。能力字段为 `files/notes/rename/delete:true`，
`media_control/wifi_provisioning:false`；后两项指手机协议不提供媒体遥控和配网指令，
本地 UI 的 MPP 能力独立于手机协议。v2 要求配对凭据，不接受旧版未鉴权业务请求。
`notify/input` 成功表示已进入服务消息队列，满队列返回 `-EAGAIN`；UI 异步处理，
对话和翻译需用户在 LVGL 中确认发送。文件更改完成后发布 UI 刷新事件；结果队列满时可能
丢弃该文件变化事件，客户端可重新查询目录。

文件名最长 120 字节，不允许以点开头或包含路径分隔符、换行。单块二进制最多 4096 字节，
以十六进制编码传输。上传从 offset 0 开始，每次使用前一次响应的 offset；最后一块设置
`final:true`，空文件发送空块。offset 0 会重建同名临时上传，客户端不要并发上传同名文件。
断点续传只接受与当前临时文件长度一致的 offset。最后一块通过原子链接发布最终文件。

首次运行创建稳定的 `device_id`，未绑定时生成六位配对码并在 LVGL 本地显示，`hello` 不返回该码。
手机先核对设备编号，再发送 `pair`，将返回的凭据保存在自己的本地存储。连续五次错误会暂停配对
30 秒。绑定保存在数据根目录的 `.phone-binding.json`（0600 权限），不在手机可访问的三个文件目录内；
进程重启后保留绑定，手机通过带凭据的 `status` 恢复连接。每个实例保存一份有效凭据。
更换为不持有该凭据的浏览器时，须在 LVGL“手机互联 → 设备配对”页面解除旧绑定。
解除操作保留已有文件，不撤回已经完成的操作。

```json
{"id":1,"op":"hello"}
{"id":2,"op":"pair","code":"屏幕上的六位数字"}
{"id":3,"op":"notify","credential":"配对返回的64字符凭据","text":"下午三点出发"}
{"id":4,"op":"input","credential":"配对返回的64字符凭据","target":"chat","text":"介绍一下这个项目"}
```

`notify/input` 的原文同时保存在服务状态的最新消息字段。LVGL 底部显示最多两行，超长内容省略；
主机调试页在画面下方展示完整文本，按普通文字呈现并保留换行，不执行 HTML。
调试页通过主机专用 `ui.sock` 读取服务状态。

标准库参考客户端见 [tools/phone_client.py](tools/phone_client.py)。从仓库根目录执行：

```bash
python3 Application/lvgl-home/tools/phone_client.py --socket .host-debug/runtime/data/bridge.sock hello
# 使用屏幕上的实际数字；命令返回 credential，由客户端妥善保存。
python3 Application/lvgl-home/tools/phone_client.py --socket .host-debug/runtime/data/bridge.sock pair 123456
# 后续命令从 AIGLASS_PHONE_CREDENTIAL 环境变量读取配对凭据。
python3 Application/lvgl-home/tools/phone_client.py --socket .host-debug/runtime/data/bridge.sock ping
python3 Application/lvgl-home/tools/phone_client.py --socket .host-debug/runtime/data/bridge.sock input chat '你好'
# TCP 还需配置 AIGLASS_PHONE_TOKEN；watch 每五秒发送心跳。
python3 Application/lvgl-home/tools/phone_client.py --host 192.168.1.10 --port 8765 watch
```

## 验证范围

主机原生构建启用 `-Wall -Wextra -Werror`。自动检查使用实际 LVGL 进程验证文件读写、
改名删除、通知、草稿、导航、笔记编辑与页面生命周期；HTTP 请求使用明确标注的本地测试服务。
ASan/UBSan 用于检查运行及退出；手机浏览器检查包含实际文件内容比对、配对鉴权和断连恢复。
命令与报告位置见 [主机验证说明](../host-debug/README.md#验证)。

[tools/check_static.py](tools/check_static.py) 使用真实依赖头文件运行 `-fsyntax-only`，并检查
Python 语法、源码空白及字体覆盖。可额外传入真实 AGLink UAPI 头文件检查受控媒体层与协议布局，
`--vendor-syntax` 使用 tree-sitter/tree-sitter-c 检查厂商 C 文件的语法树。
该入口仅执行静态检查，不生成目标文件或运行应用。在仓库根目录执行：

```bash
python3 Application/lvgl-home/tools/check_static.py --lvgl /path/to/lvgl-8.3.11 --curl-include /path/to/curl/include \
  --aglink-include /path/to/bsp/user_headers/include/bsp --vendor-syntax
```

本地 MPP 后端及厂商录像实现的完整类型检查与链接未验证，验证依赖完整 V821 Eyesee MPP
开发头文件和库。语法树检查不覆盖目标 BSP 的接口类型与 ABI。

## 布局与字体

布局以 `640 × 480` 为基准，[布局说明](docs/layout.md) 记录控件尺寸和边界；
[静态布局示意](docs/layout.svg) 用于审阅结构，不代表运行截图。
主机实际 LVGL 截图保存在 `.host-debug/reports/`；物理屏幕、触摸、按键、MPP 设备驱动和真实服务仍需板端验证。

字体由 Noto Sans CJK SC 转为 16 px、1 bpp 的 LVGL 字形数据，生成工具见
[tools/generate_font.py](tools/generate_font.py)，需 Pillow 及 Noto Sans CJK TTC 字体。
依赖及许可证见 [third_party/README.md](third_party/README.md) 和 [fonts/OFL.txt](fonts/OFL.txt)。
