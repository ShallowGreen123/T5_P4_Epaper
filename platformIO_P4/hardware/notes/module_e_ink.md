# Module Name: Electronic Paper Display (EPD) 电子纸
# EPD IC type: EK79615, 2pcs
# EPD Module Resolusion: 720(H)x1440(V), pixel size: 90.5(H)x92.5(V)
# EPD Color numbers: 16 Gray Level (monochrome)
# Screen Size: 5.84 Inch
# Schematic diagram: [reference](../datasheet/E_Ink_(ED058TC8)_final_ver_2.0.pdf)

## 1. 功能简介
- 模块用途：用于 UI 交互
- 主要功能：
- 驱动参考 lib/FastEPD

## 2. 接口信息

墨水屏直连并口，参考 [t5-p4-e-paper-schematic-review](./t5-p4-e-paper-schematic-review.md)

- 触摸型号为 GT911，地址为 `0x5D`
- 触摸复位脚：PCA9535 扩展 IO 的 BOARD_PCA_00_T_RST
- 触摸中断脚：IO3

## 3. 引脚映射
[t5-p4-e-paper-pinmap](../pinmap/t5-p4-e-paper-pinmap.md)

## 4. 上电/初始化要求

## 5. 关键寄存器/命令/协议

## 6. 风险点

## 7. 演示demo关注点
- 墨水的的基本功能，GIF 局刷、灰阶、全刷
- 触摸的可靠性
- 长时间运行稳定性
