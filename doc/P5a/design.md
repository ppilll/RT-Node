# P5a 设计

## 接线与初始化

| STM32F103ZET6 | BME280 六针模块 | 说明 |
|---|---|---|
| PB10 I²C2_SCL | SCK/SCL | 硬件 AF 开漏，100kHz |
| PB11 I²C2_SDA | SDI/SDA | 硬件 AF 开漏，100kHz |
| 3.3 V | VCC/VIN（模块电气已由用户核对） | VDD/VDDIO 在传感器绝对/工作限值以内 |
| GND | GND | 共地 |
| 3.3 V | CSB/CS | 必须高以使用 I²C，曾拉低则重新上电 |
| GND | SDO | 默认地址 0x76；确认实际 ACK |

不得把七位地址 `0x76` 直接错误传入 STM32 HAL 地址字段（HAL 默认需要 `0x76 << 1`）。初始化期间读 ID 寄存器 0xD0，要求为 0x60；不要因兼容 BMP280 而省略湿度补偿。

## 源码复用评审（根据用户上传 .ino）

`.ino` 以 Arduino `Wire` 软件接口读写 0x76，包含三项补偿算法和校准字段，**不是可直接导入 ARMCC5 的 STM32 HAL 驱动**。问题：未验证芯片 ID，I²C 收包长度及错误处理欠缺，`dig_H1`/`dig_H3` 的有符号声明不匹配 Bosch，`dig_H4`/`dig_H5` 的负值符号扩展处理不严谨；把 Normal mode 设为非睡眠后才写 config 的顺序也需注意。故将 .ino 用作参考，不整体移植。

**优先复用** Bosch BME280 SensorAPI 正式代码（`bme280.c/.h/_defs.h` 和 BSD-3-Clause 许可）。Codex 在使用时固定版本/提交来源，并核实 ARMCC5 的 C99 宏、宏冲突、整数类型及数据单位；只实现 HAL I²C 的 `read/write/delay_us` 回调，不引入 COINES、Arduino 或 SPI 适配。

Bosch API 默认可能启用 DOUBLE 补偿，也可用 `BME280_64BIT_ENABLE`/`BME280_32BIT_ENABLE`。建议优先验证整数模式以减少 Cortex-M3 浮点开销，但**具体选择以编译和单位核实为准**：不同模式的 pressure/temperature/humidity 缩放不同；禁止直接强制转换整数而不验证系数。公开快照采用独立固定单位，不暴露 Bosch 结构布局。

## 测量与发布

- `SensorTask` 低优先级、静态创建；约 500ms 周期（1kHz Tick 即 500ticks）；IMU/控制任务优先级不变。
- 首选 Forced mode，每次启动一次 T/P/H 转换（初始 oversampling 可选 x1），等待 Bosch 推荐的转换时间后读取 **同一次** 补偿数据；不要读取未转换完成的旧寄存器；避免长 busy wait/关中断。
- `HAL_I2C_Mem_Read/Write` 或简单 HAL 轮询接口，短超时；I²C 读写回调只操作 I²C2。启动 `bme280_init` 和 I²C 初始化放在适当任务/启动上下文，注意当前 HAL 与 FreeRTOS 共用 SysTick。
- 只有成功完整读取并核对数据范围后才将 `valid=true`、更新时间戳；出错保留上次成功 tick，但 `valid=false`。快照在 `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` 中完成简单结构体复制；不在临界区做 I²C/格式化。
- 如需错误计数只添加一个简单模块局部计数器。故障不停止控制任务、CAN、UART、ADC、IMU。

## P5a 修改范围建议

- 新建 `App/env/env_app.c/.h`、必要 BME280 适配层及 Bosch 源码 `Driver/bme280/`（按仓库风格调整）。
- `Core/Src/main.c` 添加非关键环境模块初始化/任务创建（失败不动现有 PWM 安全路径）。
- `Projects/RTnode.uvprojx` 添加 HAL 的 `stm32f1xx_hal_i2c.c` 及新增源/IncludePath；`stm32f1xx_hal_conf.h` 已使能 HAL I²C，无需再次重复定义。
- `App/uart/uart_app.c` 在唯一 TX 所有者内添加 `ENV?`；扩展回应缓冲要做确切上界分析，不能破坏 `PING`/`COUNT?`/`ADC?`。

## 非目标

不配置 I²C1/SSD1306；不引入 Mutex、DMA、软件 I²C；不抽象 SPI1；不重构既有状态机；不实现 P6 传感器故障管理。
