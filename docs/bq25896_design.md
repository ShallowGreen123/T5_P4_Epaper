请为 TI BQ25896 生成一份可移植的嵌入式 C++ 驱动，要求：

1. 采用分层设计：
   - bq25896_hal.h/.cpp：I2C 读写接口由用户实现
   - bq25896_reg.h：寄存器地址、bit mask、shift 定义
   - bq25896.h/.cpp：驱动 API
   - bq25896_hal_esp_idf.h/.cpp：适配 `esp-idf driver/i2c_master.h` 的 HAL 辅助实现
2. I2C 7-bit 地址通过用户初始化的时候获得。
3. 寄存器范围按 0x00~0x14 组织。
4. 提供以下 API：
   - init
   - reset
   - enable_charge / disable_charge
   - set_input_limit_ma
   - set_charge_current_ma
   - set_charge_voltage_mv
   - enable_otg / disable_otg
   - set_otg_voltage_mv
   - kick_watchdog
   - read_status
   - read_fault
   - read_adc
   - shutdown
5. 所有寄存器写入必须使用 read-modify-write。
6. 所有输入参数都做范围检查和钳位。
7. 所有 API 返回错误码，不允许 silent fail。
8. 不允许使用 magic number，全部用宏和枚举。
9. 生成 Doxygen 风格注释。
10. 增加一个轮询状态机示例，用于处理：
    - VBUS 插入/拔出
    - VINDPM/IINDPM
    - 温度异常
    - 充电完成
    - watchdog 维护
11. 使用 esp-idf 组件格式，和兼容 C++
