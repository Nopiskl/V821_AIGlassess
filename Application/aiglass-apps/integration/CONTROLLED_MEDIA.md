# 受控模式媒体 C 接口

`apps/src/media/ag_media.h` 是供自定义受控宿主使用的可选进程内接口，依赖 AGLink、RT Media
与 MPP。常电 LVGL 使用独立的 [vm_media](../../mpp-media/README.md)，不调用本接口。

## 调用与复用

| API | 行为 |
| --- | --- |
| `ag_media_open` | 取得运行模式/媒体锁，创建 AGLink 通道并持有应用回调 |
| `ag_media_photo` | 通过原录像模块的 RT Media JPEG 路径主动抓图 |
| `ag_media_record_start` | 录音接收 AGLink 音频；录像复用原 RT Media/MPP 管线 |
| `ag_media_record_stop` | 结束本地录制，录像发 VIDEO_END，录音发送通用结果 |
| `ag_media_status` | 查询当前模式、控制端停止/断线和媒体错误 |
| `ag_media_close` | 停止并关闭通道、释放资源 |

该 API 不运行七个独立应用的 `*_app_init()`，不发送 `AG_VD_POWER_OFF`，不提供串口命令
解析。自定义宿主负责业务状态和电源策略；进程内 API 与独立应用的完成通知、关机流程分别定义。
独立应用的行为见 [AGLink 协议](AGLINK_PROTOCOL.md)。

所有外部调用由同一个工作线程串行执行，只允许一个实例。公共层持有 AGLink 应用回调，
不能同时启动其他独立应用或另行初始化全局 RT Media/MPP。AGLink 回调在锁内处理音频，
控制线程负责停止和释放，不在回调中销毁媒体管线。

默认 `dev_id=0`、照片通道 4、JPEG 质量 80；照片通道须为已启用的 YUV 预设。
照片宽高均为 0 时优先读取预设 JPEG 尺寸，否则取通道分辨率。录像使用通道 0 的 DTS 预设。
输出写入调用方路径，不覆盖同名文件，不更新旧 `media.config`，不导出陀螺仪侧文件。

## 格式与协议边界

录像开始发送 `AG_VD_VIDEO_START`，停止发送 `AG_VD_VIDEO_END`，响应 `AG_AD_VIDEO_STOP`。
录像音频按原应用要求接收单声道、16 kHz、16-bit PCM，交给 MPP AAC 编码。

录音按顺序保存 `AG_AD_AUDIO` 载荷，不添加 Ogg 页、不执行 Opus 编码或 WAV 转换。
本地停止关闭文件后发送 `AG_VD_GENERIC_RESPONSE`；没有收到任何数据时返回 `-ENODATA`。
UAPI 没有 `AG_VD_RECORD_AUDIO_STOP`，控制端必须自行停止发送音频。

RT Media/MPP 调用为同步接口；取消与销毁必须等待正在执行的厂商调用返回。
接口不提供音频播放、输出音量控制或相机实时预览。

## 开发库

在完整 BSP 中，`make -C Application media AIGLASS_BSP_ROOT=/path/to/bsp` 生成：

```text
Application/aiglass-apps/output/dev/include/ag_media.h
Application/aiglass-apps/output/dev/lib/libaiglass_media.a
Application/aiglass-apps/output/dev/lib/aiglass-media.ldflags
```

归档只含 `take_video.c` 和 `ag_media.c`，没有 `main.c`。宿主对象之后链接归档，再使用
ldflags 文件中的 BSP 依赖。参数文件含当前 BSP 的绝对路径，移动或切换 BSP 后须重新生成。
`all` 同时生成七个独立应用及该可选库；rootfs overlay 不打包开发归档。

## 验证范围

公共层的静态检查使用 AGLink UAPI 头文件；录像实现的检查范围限于 C 语法树与代码审查。
目标构建与运行未验证。录像实现的完整类型检查、链接及板端验证需要完整 V821 MPP 开发头文件和库。
