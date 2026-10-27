# Linux 主机联合调试

Linux 主机开发环境在本机编译并运行 `lvgl-home`，通过浏览器同时操作 LVGL 和 uni-app 手机端。
手机端文件操作实际读写主机运行目录，文字消息进入 LVGL 服务与界面。无需显示器、桌面会话、
SDL、V821 BSP 或交叉工具链；不访问摄像头、麦克风、背光或 JL。

设计与维护说明见 [Linux 主机模拟器架构](ARCHITECTURE.md)，包含源码复用、进程与线程、
显示输入、通信与配对、文件传输、测试边界和设备集成约束。

## 启动与远程预览

在仓库根目录执行。需要 Python 3.10+、CMake、C11 编译器、POSIX threads、libcurl 运行库和
Node.js 20+。工具从 `PATH` 查找，也可将 Node.js 安装在 `.host-debug/tools/node`。

```bash
export PATH="$PWD/.host-debug/tools/node/bin:$PATH"
export TMPDIR="$PWD/.host-debug/tmp"
export PYTHONDONTWRITEBYTECODE=1
mkdir -p "$TMPDIR"
python3 Application/host-debug/host.py build
python3 Application/host-debug/host.py run
```

`build` 在工作区下载并校验锁定的 LVGL 源码及 curl 头文件，构建原生程序和 H5；首次缺少
`node_modules` 时执行 `npm ci`。依赖安装范围限于工作区。源码 URL 与 SHA-256 见
[dependencies.json](dependencies.json)，依赖保留原始许可证。
curl 头文件固定为 8.10.1，运行库使用主机版本；HTTP 选项包含旧运行库兼容路径。

VS Code Remote SSH 的“端口”面板中添加 **8787**，在本地浏览器打开：

- `http://localhost:8787/debug/`：左侧实际 LVGL 画面，右侧手机页面。
- `http://localhost:8787/`：单独打开手机页面。

若 VS Code 分配了不同的本地端口，以面板中的转发地址为准。只需转发这一个端口。
服务监听远端 `127.0.0.1`，无需开放服务器公网端口。

默认画面为 **640 × 480 横屏**。首次启动，LVGL 显示设备标识和六位配对码。
手机选择“查找设备”，核对标识并输入配对码，成功后进入主页面。绑定在两端保存，后续自动恢复；
更换浏览器时，先从 LVGL“手机互联 → 设备配对”解除原绑定。该流程完成应用授权，不执行设备 Wi-Fi 配网。
手机端连接运行中的 LVGL 服务，服务不可用时显示离线。
手机发送的消息在 LVGL 底部显示，并在预览图片下方显示完整文本。
左侧可点击 LVGL 画面、使用页面选择框或方向键/Enter/Escape/Tab。编辑文字时先在 LVGL 中
点击“输入”或“编辑”，再用下方文本框填入中文，最后在 LVGL 中点击“完成”。
`Ctrl+C` 结束启动命令会关闭桥接和原生子进程，保留运行数据。也可指定分辨率与端口：

```bash
python3 Application/host-debug/host.py run --width 640 --height 480 --port 8787
```

宽度范围为 640–1024、高度范围为 480–1024 像素，默认 640 × 480。每个运行目录只允许一个原生实例；默认目录为
`.host-debug/runtime`。独立实例使用 `--runtime another-run --port 8789`，运行目录始终位于
`.host-debug` 内。开发手机界面时可另启 `npm run dev:h5`，其 `/api/host` 代理指向 8787。

## 接入方式

```text
uni-app H5
  └─ HTTP /api/host/rpc
       └─ bridge.py
            └─ runtime/data/bridge.sock
                 └─ aiglass_phone.c → 文件服务 / LVGL 消息队列

联合调试网页
  ├─ /api/host/frame.png ← 原生 LVGL flush 回调生成的画面
  └─ /api/host/ui → runtime/ui.sock → LVGL 主线程的输入和控件事件
```

原生程序直接编译 `aiglass_home.c`、小游戏、服务、手机协议、HTTP、字体和 cJSON，
使用 LVGL 8.3.11 软件绘制及指针输入。网页展示原生 LVGL 帧缓冲输出。
`ui.sock` 仅用于主机调试，不属于手机/设备正式协议。

主机与设备编译同一份 UI、配对、服务、手机协议及 HTTP 源码。共用源码不使用主机专用
条件编译，平台差异限定在以下入口：

| 部分 | Linux 主机 | V821 设备 |
| --- | --- | --- |
| 显示与输入 | 软件帧缓冲、浏览器指针与按键 | 宿主 LVGL 显示和输入驱动 |
| 服务配置 | `hardware_access=false` | 默认 `true` |
| 媒体链接 | [media_unavailable.c](media_unavailable.c)，返回 `ENOTSUP` | `mpp-media/vm_media.c`，使用 V821 MPP |

共用 UI 根据能力字段禁用硬件按钮，服务层同时拒绝硬件操作。主机不打开 evdev、不取得设备模式锁，
不访问 sysfs 背光或电池。
JL 固件、厂商独立应用和板端媒体能力不属于主机模拟器的执行范围。

## 功能与限制

| 功能 | 主机模式行为 |
| --- | --- |
| 连接、状态 | 首次配对、持久绑定、解除与自动重连；查询实际 LVGL 服务；每 3 秒刷新，失败显示离线，恢复后可重连。电量未知显示 `--` |
| 文件管理 | 列出 `inbox`、`notes`、`media`，预览文本/图片、改名、删除实际主机文件 |
| 双向传输 | 浏览器 IndexedDB 与主机文件按 4096 字节分块传输；暂停/继续、取消、失败重试、刷新后恢复任务 |
| 文件发布 | 上传完成后原子发布，不覆盖同名文件；下载完整后才出现手机文件。失败重试从头开始，暂停继续保留已确认偏移 |
| 笔记 | 手机创建 `notes/phone-*.txt`；LVGL 编辑保存 `notes/note.txt`，手机可读取、下载、导出 |
| 通知 | 投送到 LVGL“手机互联”页和底部常驻消息区；完整文本在预览图片下方显示 |
| 对话/翻译草稿 | 手机投送后切换到对应 LVGL 页面；用户在 LVGL 确认“发送”后才调用配置的 HTTP 接口 |
| 导航文字 | 更新 LVGL 导航页面；手机入口将速度、里程置零，不提供定位和路线规划 |
| 文本浏览、下载 | LVGL 可读取 HTTP(S) 网页文字、下载文件至 inbox；不执行网页脚本或安装文件 |
| 小游戏、专注 | 使用现有 LVGL 本地实现；专注进度保存至 notes。手机商店不安装 LVGL 应用 |
| 设置 | 手机主题、昵称、紧凑列表、完成提醒均为浏览器本地设置 |
| 硬件能力 | 主机不提供拍照、录音、录像、实时语音、背光、真实配网或 AGLink/JL 通信 |

AI 功能需要配置外部服务。启动前设置 `AIGLASS_CHAT_URL`、`AIGLASS_MODEL`、
`AIGLASS_API_KEY`，约定见 [LVGL HTTP 服务说明](../lvgl-home/README.md#http-服务约定)。
自动测试仅使用带 `[HTTP 测试回执]` 标记的本地测试响应，不能用于评价模型和翻译效果。

单文件上限 32 MiB，文件名上限 120 UTF-8 字节；每目录最多列出 32 项，不支持分页。
普通文字最多 2047 字节，导航最多 255 字节。协议按路径读写，不提供文件快照或多客户端上传仲裁。
传输期间须保持源文件不变，并避免多窗口并发上传同名文件。手机侧绑定、文件元数据和任务保存在
浏览器本地状态中，文件内容保存在 IndexedDB；服务侧绑定保存在运行目录。

## 协议与目录

`POST /api/host/rpc` 需要 `Content-Type: application/json` 和 `X-Aiglass-Host: 1`，请求/响应
使用 [手机通信协议 v2](../lvgl-home/README.md#手机通信协议-v2)。桥接只转发允许的操作，
不执行 shell 命令，不接收任意 socket 路径，不开放跨域访问。

业务请求必须携带配对后的 `credential`；未配对只允许 `hello` 与 `pair`。`hello/ping/status` 返回运行环境及能力。
`media_control:false`、`wifi_provisioning:false` 表示手机协议未提供相应指令。
`notify/input` 成功响应表示消息已进入服务队列，UI 由主线程异步更新，不表示 AI 请求已完成。

```text
Application/host-debug/          主机入口、桥接、构建配置、调试网页与测试源码
.host-debug/
  deps/                         校验后的第三方源码、头文件
  tools/                        工作区 Node.js、测试浏览器
  cache/                        下载缓存、npm 缓存
  tmp/                          编译及浏览器临时文件
  build/、build-asan/            原生构建产物
  runtime/                      日常联调的 socket、帧缓冲和 data/{inbox,notes,media}
  test-*/、browser-*/            每次测试的独立运行目录
  logs/、reports/               构建、服务、测试日志、截图和失败 trace
```

`.host-debug/` 由根目录 Git 规则忽略。H5 依赖和产物位于工作区内的
`Application/mobile-app/node_modules`、`dist`。

## 验证

构建后运行测试。测试服务默认使用 8788，与日常 8787 实例隔离；也可传入 `--port`。
浏览器配置对应锁定的 Playwright 1.64.1 / Chromium headless shell 1248。

```bash
export PATH="$PWD/.host-debug/tools/node/bin:$PATH"
export TMPDIR="$PWD/.host-debug/tmp"
export PLAYWRIGHT_BROWSERS_PATH="$PWD/.host-debug/tools"
export PYTHONDONTWRITEBYTECODE=1
# 首次准备测试浏览器时执行：
node Application/mobile-app/node_modules/@playwright/test/cli.js install chromium --only-shell
python3 Application/host-debug/tests/run.py

python3 Application/host-debug/host.py build --sanitize
AIGLASS_TEST_BINARY="$PWD/.host-debug/build-asan/aiglass-host" \
  python3 Application/host-debug/tests/test_native.py
```

测试按以下层次组织：

| 层次 | 覆盖范围 |
| --- | --- |
| 原生集成 | 配对持久化与撤销、凭据校验、文件内容与路径校验、编辑器、消息、16 个页面的重复切换、HTTP 文本与下载契约 |
| 浏览器联调 | 首次配对、绑定恢复、文件暂停与恢复、失败重试、断连恢复、图片下方消息布局 |
| ASan/UBSan | 内存访问、未定义行为及正常退出路径 |

默认分辨率为 640 × 480，测试记录和截图保存在工作区。设备 MPP、物理屏幕、麦克风、功耗及
JL 协议互通属于板端验证范围。测试结构与运行边界见 [测试设计](ARCHITECTURE.md#13-测试设计)。
