# SC2336 / OV2710 / OV5645 供电要求

本文记录 SC2336、OV2710、OV5645 三颗摄像头传感器的供电域和 T5-P4 E-Paper 板级供电之间的对应关系。

## 板级供电结论

T5-P4 E-Paper 当前文档和示例中的板级假设是：

| 板级器件 | 输出 / 网络 | 目标电压 | 用途 |
| --- | --- | --- | --- |
| SGM38121 `AVDD1` | `CAM_1V8` | 1.8 V | 摄像头 1.8 V 电源 |
| SGM38121 `AVDD2` | `CAM_2V8` | 2.8 V | 摄像头 2.8 V 电源 |

也就是说，当前板侧默认只提供 `CAM_1V8` 和 `CAM_2V8` 两路相机电源。对带内部稳压或外部电源整理的相机模块，需要按模块原理图确认这些网络实际接到了哪些 sensor 电源脚；不要把模块引脚和裸 sensor 引脚直接等同。

## 传感器裸片供电域

| Sensor | 供电域 / 常见引脚名 | 电压要求 | 说明 |
| --- | --- | --- | --- |
| SC2336 | `AVDD` / `AVDD1` / `AVDD2` | 2.8 V ± 0.1 V | 模拟电源 |
| SC2336 | `DOVDD` / `DOVDD1` / `DOVDD2` | 1.8 V ± 0.1 V | 数字 I/O 电源 |
| SC2336 | `DVDD` / `DVDD1` / `DVDD2` / `DVDD3` | 待按具体 datasheet / 模块确认；常见参考设计中可能是 1.2 V | 公开产品页主要列出 2.8 V analog 和 1.8 V I/O；若使用裸 CSP 或未集成稳压的模组，必须再核对 DVDD。 |
| OV2710 | `AVDD` | 3.0 V ~ 3.6 V，典型 3.3 V | 模拟电源 |
| OV2710 | `DOVDD` | 1.7 V ~ 3.6 V，典型 1.8 V | 数字 I/O 电源 |
| OV2710 | `DVDD` | 1.425 V ~ 1.575 V，典型 1.5 V | 核心电源；DOVDD 为 1.8 V 时 datasheet 推荐使用内部 regulator，DOVDD 为 2.8 V 时推荐外供 1.5 V DVDD。 |
| OV2710 | `PVDD` / `EVDD` / `SVDD` | 需按参考设计连接 | PLL、MIPI core、sensor circuit 等相关电源域，不能只按 `CAM_1V8/CAM_2V8` 猜。 |
| OV5645 | `AVDD` | 2.6 V ~ 3.0 V，典型 2.8 V | 模拟电源 |
| OV5645 | `DOVDD` | 1.71 V ~ 3.0 V，典型 1.8 V / 2.8 V | 数字 I/O 电源 |
| OV5645 | `DVDD` | 1.425 V ~ 1.575 V，典型 1.5 V | 核心电源；DOVDD 为 1.8 V 时 datasheet 推荐使用内部 DVDD regulator，可不外供 1.5 V DVDD。 |
| OV5645 | `PVDD` / `EVDD` | 需按参考设计连接 | `PVDD` 为 PLL 电源，`EVDD` 为 MIPI TX / core 相关电源域。 |

## 对当前 T5-P4 配置的判断

- `CAM_1V8 = 1.8 V`、`CAM_2V8 = 2.8 V` 对 OV5645 比较自然：`DOVDD` 可接 1.8 V，`AVDD` 可接 2.8 V，`DVDD` 通常可由内部 regulator 处理，但仍要看模块是否这样设计。
- `CAM_1V8 = 1.8 V`、`CAM_2V8 = 2.8 V` 对 SC2336 的公开产品页电压也比较接近：analog 2.8 V、I/O 1.8 V；但裸片 DVDD 是否另需 1.2 V，需要按具体 SC2336 datasheet 或模组原理图确认。
- 对裸 OV2710 要特别小心：其 `AVDD` 典型是 3.3 V，`CAM_2V8` 直接当 OV2710 `AVDD` 可能偏低。只有在模组上有额外稳压/升压，或模组资料明确允许 2.8 V 模拟供电时，才应按当前两路供电继续。

## 代码中的默认值

当前 `camera_wifi_stream` 示例使用：

```text
CONFIG_CAMERA_WIFI_STREAM_AVDD1_MV=1800
CONFIG_CAMERA_WIFI_STREAM_AVDD2_MV=2800
```

含义是通过 `SGM38121` 输出：

```text
AVDD1 -> CAM_1V8 -> 1.8 V
AVDD2 -> CAM_2V8 -> 2.8 V
```

这描述的是板级电源网络，不等价于三颗 sensor 的所有裸片供电脚都已经满足。

## 参考资料

- 本仓库板级连接：[pinmap.md](pinmap.md)
- 本仓库 SGM38121 示例：[examples/sgm38121/README.md](../examples/sgm38121/README.md)
- SC2336 产品资料：<https://www.photonicsgo.com/product/cmos-image-sensors/sc2336>
- OV2710 datasheet / 产品页：<https://datasheet.lcsc.com/lcsc/OmniVision-Technologies-OV02710-A68A-1E_C114923.pdf>、<https://www.ovt.com/products/ov2710/>
- OV5645 datasheet / 产品页：<https://www.pdapply.com/upload/OV5645_CSP3_DS_2.01_.pdf>、<https://www.ovt.com/products/ov5645/>
