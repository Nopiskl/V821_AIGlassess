# 第三方代码与字体来源

| 内容 | 版本 / 来源 | 许可证 |
| --- | --- | --- |
| `cJSON.c`、`cJSON.h` | [DaveGamble/cJSON v1.7.19](https://github.com/DaveGamble/cJSON/tree/v1.7.19)，保持上游源码 | [MIT](cJSON-LICENSE) |
| `../fonts/aiglass_font_16.c` | [Noto Sans CJK SC](https://github.com/notofonts/noto-cjk) 2.004，使用本模块生成工具转换；Copyright 2014-2021 Adobe | [SIL Open Font License 1.1](../fonts/OFL.txt) |

设备构建的 LVGL、libcurl 由宿主工程提供；常电媒体接口来自仓库内 `mpp-media`，其 MPP
开发头文件及运行库由目标 V821 BSP 提供。

主机构建固定 LVGL v8.3.11 源码和 curl 8.10.1 头文件，版本、下载地址与 SHA-256 见
[dependencies.json](../../host-debug/dependencies.json)，libcurl 运行库使用主机版本。
静态语法检查以对应依赖头文件为依据；目标 BSP 的库链接和运行兼容性需要单独验证。
