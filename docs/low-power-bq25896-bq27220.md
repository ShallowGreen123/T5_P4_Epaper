
## 1. 低电压自动关机

factory 添加检测电池低电压过低自动关机的功能，避免电池过度放电；

电池信息
- 电池的额定容量：1100mAh
- 额定电压：3.7V
- 充电目标电压：4208mV
- 快充电流：512mA
- 预充电流：128mA
- 终止电流：128mA
- 输入限流：1000mA
- 系统掉电保护：3300mV

现在当电池进入低电量条件时，会在 lv_layer_top() 弹出“电量过低，请充电”(使用英文提示词)，显示 20 秒倒计时；倒计时结束且仍未插电时，通过 BQ25896 关机。插入充电后弹窗会立刻消失。

低电压判断只关注 BQ27220 状态，忽略 BQ25896，详见“两颗芯片的分工”

自动关机标准，满足其中一条可以触发低电压保护；
<!-- - BatteryStatus(): 0x0A and 0x0B 的 SYSDWN 位被置1
- GaugingStatus: 0x0056 的 EDV 位被置1 -->
- BQ27220 的电压寄存器 CommandVoltage (0x08u) 的电压低于 3300mv

## 2. 两颗芯片的分工

- `BQ25896`: 充电器。负责输入限流、预充、恒流、恒压、终止、再充、系统最小电压。
- `BQ27220`: 电量计。负责 `SOC/FCC/SOH`、温度/电流/电压跟踪、满充判定、休眠/relax 状态。

要稳定，关键不是“两个芯片都能读到”，而是它们对同一块电池使用同一组参数：

- `BQ25896.VREG` 要和 `BQ27220.Charging Voltage` 一致
- `BQ25896.ICHG` 要和 `BQ27220.Charging Current` 同量级
- `BQ25896.ITERM` 要和 `BQ27220.Taper Current` 同量级

如果这三组不对齐，最常见的现象是：

- 满充识别不稳定
- 拔掉 USB 后 `SOC` 突跳
- 长时间学不准 `FCC`
- 明明“充满”，但 gauge 不置 `FC`

## 3. 稳定工作流

1. 先把 BQ25896 拉回正常充电态。
现在工程里的顺序是：
resetDefault -> disableWatchdog -> exitHizMode -> disableOTG -> enableBatterPowerPath -> 重新配置充电参数 -> enableCharge；

2. 再初始化 BQ27220。
它会：读取芯片 ID -> unseal -> 检查 INITCOMP/CFGUPDATE -> 检查 profile 和 DataMemory -> 不对就 reset + 重写参数 -> seal；

3. 运行时由 BQ25896 决定“有没有真的充电”，由 BQ27220 负责“当前电池看起来像什么状态”。如果 VBUS 已接入，但 BQ25896 还没进充电态，或者 BQ27220 还在报负电流放电，就自动再恢复一次；

## 4. 需要注意的地方
- 先把 BQ25896 电源路径拉到正确状态，再让 BQ27220 用正确模型去计量。
- 不要用 BQ27220 判断 USB 是否插入，USB/充电源状态一定看 BQ25896。
- 不要把 BQ27220.getChargeCurrent() 当成实测充电电流，它更像 gauge 给出的充电建议值；实测是否在充电，优先看 BQ25896 CHG_STATUS 和 BQ27220 AverageCurrent。
- 不要把 !DSG 直接当成“正在充电”，中间还有 Relaxation。PPM.shutdown() 之后，必须显式恢复 BATFET/HIZ/OTG/CHG_CONFIG，否则很容易出现“USB 在，但系统还在吃电池”的假上电状态。
- BQ27220 的模型参数要和真实电芯匹配；模型错了会导致电量百分比不准，但不会直接导致“充不进去电”。低电量保护最好用“实际电压 + 充电器状态”做主判据，别只靠 gauge 的粘性状态位。

## 5. 第一轮验收标准
1. 插拔 USB 后 `BQ25896` 寄存器不莫名回默认
2. 充电时 `VBUS/SYS/BAT` 变化平滑，不频繁进出 DPM
3. 可以充到100%满电；
4. 拔掉 USB 后 `SOC` 不会瞬间大跳

