# PCA9535 ESP-IDF Example

这个例程已经改成纯 `esp-idf` 写法，默认目标芯片是 `esp32p4`。

依赖优先走 ESP Component Registry：

- `wngfra/esp_io_expander_pca9535`
- `espressif/esp_io_expander`

例程功能：

- 初始化 `GPIO7/8` I2C
- 连接地址 `0x20` 的 `PCA9535`
- 将 16 路扩展 IO 配成输入
- 每 `500ms` 读取一次当前 IO 状态并打印

编译：

```bash
idf.py -C examples/pca9535 build
```

烧录：

```bash
idf.py -C examples/pca9535 -p PORT flash monitor
```
