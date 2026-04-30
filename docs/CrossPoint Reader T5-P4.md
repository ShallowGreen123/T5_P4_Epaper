# CrossPoint Reader T5-P4 示例移植

## Summary
- 新建 `examples/crosspoint_reader_t5_p4`，保留现有 `examples/epub_reader_t5_p4` 不动。
- 范围锁定 EPUB + TXT/Markdown 阅读：文件扫描、打开书、目录/章节、翻页、进度缓存、基础状态栏。
- 不移植网络、KOReader Sync、OPDS、OTA、完整设置 UI、XTC、X4 电源/按键层。

## Key Changes
- 复用现有 T5-P4 适配：FastEPD 显示、GT911 触摸、SDSPI SD 卡、PSRAM/sdkconfig 默认值。
- 新增 `components/crosspoint_core`，移入 CrossPoint 阅读核心：`Epub`、`Txt`、分页/缓存、Zip/inflate、Expat/uzlib、UTF-8、序列化、EpdFont/GfxRenderer、日志。
- 新增 ESP-IDF 平台 shim：替换 Arduino/X4 依赖，包括 `millis`、`delay`、`ESP.getFreeHeap`、最小 `Print`、VFS/SD-backed `HalStorage/FsFile`、`esp_log` 日志。
- 渲染使用 CrossPoint 的 `GfxRenderer/EpdFont` 做文本测量和分页，再适配到 FastEPD framebuffer/刷新；图片优先用现有 JPEG/PNG 解码路径，失败时显示占位而不让章节加载失败。
- 交互采用触摸：书库列表选择；阅读页左右区域翻页；底部/中心菜单提供返回、TOC、刷新、重新扫描。
- 固定默认阅读参数：Noto Serif 18、36px 页边距、EPUB justified、embedded CSS 开、图片开、hyphenation 默认关；以后再加设置 UI。

## Interfaces
- 新示例入口：`examples/crosspoint_reader_t5_p4`，ESP-IDF/CMake 独立构建。
- `crosspoint_core` 只暴露阅读所需接口，不引入源项目的网络、设置、同步和 X4 HAL。
- README 说明 SD 布局、构建命令、触摸操作、已知限制和 CrossPoint MIT 代码来源。

## Test Plan
- 构建：`idf.py -C examples/crosspoint_reader_t5_p4 set-target esp32p4`，再 `idf.py -C examples/crosspoint_reader_t5_p4 build`。
- 硬件烟测：挂载 SD，扫描 `/sdcard/books` 和根目录，打开 EPUB/TXT/MD，前后翻页，返回列表。
- EPUB 场景：EPUB2 NCX、EPUB3 nav、多章节、长章节分页缓存、进度恢复、TOC 跳转、简单 JPEG/PNG 图片。
- TXT 场景：UTF-8、长行换行、CRLF/LF、大文件索引缓存、进度恢复。
- 失败场景：无 SD、空书库、损坏 EPUB、图片缺失、缓存不可读，均显示错误页并可重试/重新扫描。

## Assumptions
- 这是阅读示例，不是完整 CrossPoint 固件替换。
- 当前仓库里 `examples/factory` 的已有 dirty changes 与本任务无关，不触碰。
- CJK 字体覆盖不额外承诺，除非 CrossPoint 已带的字体本身支持。
