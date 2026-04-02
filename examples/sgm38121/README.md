# SGM38121 ESP-IDF Test

这个目录是一个独立的 `esp-idf` 测试工程，默认目标芯片是 `esp32p4`。

工程按当前仓库里的板级资料做了如下假设：

- `SGM38121` I2C 地址是 `0x28`
- I2C 引脚是 `SDA=GPIO7`、`SCL=GPIO8`
- `AVDD1` 接到 `CAM_1V8`
- `AVDD2` 接到 `CAM_2V8`

对应资料可以参考：

- [docs/pinmap.md](../docs/pinmap.md)
- [hardware/T5-P4 E-paper V0.1.pdf](../hardware/T5-P4%20E-paper%20V0.1.pdf)
- [hardware/SGM38121.pdf](../hardware/SGM38121.pdf)

## 功能

- 初始化 I2C 主机
- 探测 `0x28`
- 读取 `CHIP_REV`
- 回读并打印 `0x00` 到 `0x0F` 寄存器
- 默认把 `AVDD1` 配到 `1800mV`、`AVDD2` 配到 `2800mV`
- 通过 `0x0E` 使能 `AVDD1/AVDD2`
- 周期性打印当前寄存器状态

## 编译

```bash
idf.py -C sgm38121 build
```

## 烧录与监视

```bash
idf.py -C sgm38121 -p PORT flash monitor
```

## 可配项

可以通过 `idf.py -C sgm38121 menuconfig` 调整：

- I2C 引脚和频率
- AVDD1/AVDD2 目标电压
- 是否在上电时自动下发配置
- 是否周期性 dump 全寄存器
- 是否做输出翻转测试

## 注意

根据 `SGM38121` datasheet：

- 外部 `EN` 引脚拉高时，芯片会按硬件默认值直接上电
- 如果希望完全由 I2C 控制，`EN` 应保持低电平

所以如果你看到寄存器能写进去，但实测电压没有变化，优先检查板上 `EN` 管脚的连接方式。
