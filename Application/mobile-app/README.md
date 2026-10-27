# V821 · 视界

基于 uni-app、Vue 3 和 Vite 的独立手机伴侣，构建目标为 H5。
提供首次设备配对、主页面、功能、商店和我的页面。手机端通过 HTTP 桥接连接实际运行的
LVGL 进程，文件写入运行目录，文字投送至服务消息队列。

## 启动与连接

在仓库根目录执行，需要 Node.js 20+、Python 3.10+ 和主机构建依赖：

```bash
export PATH="$PWD/.host-debug/tools/node/bin:$PATH"
export PYTHONDONTWRITEBYTECODE=1
python3 Application/host-debug/host.py build
python3 Application/host-debug/host.py run
```

VS Code Remote SSH 转发 **8787**，打开 `http://localhost:8787/debug/`，左侧为实际 LVGL
640 × 480 横屏画面，右侧为手机页面；`http://localhost:8787/` 可单独打开手机端。

1. 首次启动，LVGL 显示设备标识和六位配对码。
2. 手机选择“查找设备”，核对标识，输入屏幕上的配对码。
3. 配对成功后进入主页面；后续刷新或重启自动恢复连接。
4. 更换浏览器时，先在 LVGL“手机互联 → 设备配对”解除旧绑定，再输入新配对码。

配对由共用的 C 服务完成。手机保存设备标识与凭据，业务请求通过服务验证后执行；连接失败
显示离线并保留绑定。配对流程仅完成应用授权，不包含 Wi-Fi 配网、蓝牙发现或 JL 通信。

完整构建、运行目录与调试说明见 [host-debug](../host-debug/README.md)，配对、协议、传输和
存储设计见 [模拟器架构](../../docs/architecture/host-simulator.md)。只修改手机界面时，
可在主机服务运行期间执行：

```bash
cd Application/mobile-app
npm ci
npm run dev:h5       # 5173；/api/host 代理至 8787
npm run build:h5     # dist/build/h5
npm run preview     # 4173；同样需要 8787 的 LVGL 服务
```

项目采用 hash 路由。独立部署静态 H5 还需要对应的协议网关；浏览器不直接访问 Unix socket。
小程序和原生 App 不在构建目标中；其他 WebView 的兼容性未验证。

## 功能与边界

| 功能 | 行为与限制 |
| --- | --- |
| 设备连接 | 六位码首次配对、自动重连、手动断开/重连、解除绑定；同时支持一个绑定 |
| 文件管理 | 列出实际 inbox/media/notes，预览文本/图片、改名、批量删除；每目录最多 32 项 |
| 双向传输 | 实际文件按 4096 字节分块传输，暂停/继续、取消、失败重试、刷新后恢复，不覆盖同名文件 |
| 笔记同步 | 手机创建 notes 文件；LVGL 编辑的笔记可在手机读取、下载与导出 |
| 消息投送 | 内容进入 LVGL 手机页和底部消息区；预览图下方保留完整文字与换行 |
| AI / 翻译草稿 | 投送至相应 LVGL 页面，等待用户在眼镜端确认；服务地址需另行配置 |
| 导航卡片 | 发送路线文字，速度和里程字段置零；不提供定位或路线规划 |
| 媒体遥控 | 相机、录音、录像依赖设备端，主机不支持，按钮禁用 |
| 商店 | 本地工具和模板目录、搜索、收藏、应用手机主题；不提供付款、安装或固件升级 |
| 我的 | 本地昵称、深浅主题、紧凑列表、传输完成提醒、收藏及记录清理 |

协议见 [LVGL 手机通信协议 v2](../lvgl-home/README.md#手机通信协议-v2)。`notify/input` 的成功响应
表示已进入服务队列，UI 异步更新，不代表 AI 请求完成。文件上限 32 MiB，文件名最多 120 个
UTF-8 字节，普通文字最多 2047 字节，导航文字最多 255 字节。

解绑会取消未完成任务并保留文件。断开连接或刷新页面会暂停任务，可随后继续；失败重试从头开始。
协议不提供源文件快照或多客户端同名上传仲裁，传输过程中须保持源文件不变。

## 结构与数据

```text
src/
  pages/               四个主页、配对、文件、遥控、内容同步、设备管理
  components/          页面容器、底栏、弹层、图标与文件缩略图
  services/
    device.js          单一设备服务、响应式状态与路由保护
    host-device.js     配对凭据、实际协议调用、文件传输与状态轮询
    limits.js          共用文件大小上限
    blob-store.js      IndexedDB 文件保存与导出
    catalog.js         本地商店内容
  static/              眼镜产品示意图和图标素材
  styles/theme.css     全局样式及深浅主题
```

配对凭据、文件元数据和设置保存在 `v821-companion.host.v1`；手机文件内容保存在
`v821-companion-files` IndexedDB。仅显式传输时发送文件到已配对服务，不包含账号或云同步。
旧版模拟器存储与上述命名空间独立，不读取、迁移或自动删除。清理浏览器站点数据会删除手机
本地内容，LVGL 工作目录中的文件保持不变；凭据丢失后需在 LVGL 解除旧绑定再配对。

## 测试

构建后，在仓库根目录运行 `python3 Application/host-debug/tests/run.py`，覆盖原生 LVGL 与
手机浏览器联调；`npm test` 调用同一测试入口。浏览器用例包含真实文件内容比对、笔记编辑、
消息投送、首次配对、重连、解除绑定、失败重试和预览图下方消息排版。
报告、截图、浏览器依赖和测试数据均保存在工作区 `.host-debug` 下。

[主页面](docs/screenshots/home.png) · [功能](docs/screenshots/features.png) ·
[商店](docs/screenshots/store.png) · [我的](docs/screenshots/me.png) ·
[首次连接](docs/screenshots/welcome.png) · [输入配对码](docs/screenshots/setup.png)

## 视觉与素材

配色、卡片及深浅主题参考 [AICERT_APP](https://github.com/Nopiskl/AICERT_APP)。
设备展示、导航和文件布局独立实现，应用不依赖该项目的登录或 Supabase 服务。
[硬件参考项目的“配套安卓软件”](https://oshwhub.com/sgsax/project_tiqnqiyt) 用于理解设备管理与
文件传输流程。眼镜图为生成的产品示意图，保持原始比例；见[素材说明](docs/ASSETS.md)。

## 构建依赖

工程采用 [uni-app CLI](https://uniapp.dcloud.net.cn/quickstart.html)，依赖由 `package-lock.json` 固定。
Node.js 22 下 uni-app CLI 的循环依赖提示属于已知构建日志，构建结果以命令退出状态为准。
