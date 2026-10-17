# 厂商参考 RAR 静态审查记录

## 材料与使用边界

- 用户文件：`1-HAL ICM20602 Hardware SPI.rar`，RAR5，**SHA-256** `39d641567aae438324e12bf9440aa2a7b1a461979cd922ef6f20e091214ba791`。
- 已通过压缩包读取实际文件；参考路径包括 `Inc/SEEKFREE_ICM20602.h`、`Src/SEEKFREE_ICM20602.c`、`Inc/main.h`、`Src/main.c`、`Src/stm32f1xx_hal_msp.c`、工程 `.ioc`。
- 待用户提交的计划位置：`Resources/vendor/ICM20602/1-HAL ICM20602 Hardware SPI.rar`。**文档编写时未声称仓库中已存在该文件**。Codex 若看到不同路径先查实际 Resources，不凭空创建厂商文件。
- 厂商驱动版权/用途声明应原样保留在 Resources。正式新代码自行编写、使用官方协议，不复制整个演示驱动、完整 STM32 HAL、启动文件或第三方版权内容进正式构建。

## 经文件内容验证的发现

| 发现 | 源文件依据 | P2a 的做法 |
|---|---|---|
| 示例 SPI1 Mode3，预分频8，在 72MHz 时为9MHz | `Src/main.c`，`MX_SPI1_Init` | Mode3 可参考；改为 /128 ≈0.5625MHz 初测 |
| 示例片选为 PC4，GPIO 初始化输出初值 LOW | `Inc/main.h` + `Src/main.c` `MX_GPIO_Init` | 改为 CS 在 SPI 初始化之前高电平、仅事务期拉低 |
| SPI 传输 `HAL_SPI_Transmit...(...,500)` | `Src/SEEKFREE_ICM20602.c` | 缩成经时序审查的有界毫秒超时，检查 HAL 返回并必定释放 CS |
| `while(0x12 != dat)` 无上界等 ID | 同上 `icm20602_self_check` | 最多有限探测后 FAILED，任务仍可调度 |
| `do...while(0x41 != val)` 无上界等复位 | 同上初始化 | 设截止 tick、重置失败即 FAILED；不要无限 spin |
| 配置 `GYRO_CONFIG=0x18` (±2000dps)，却把角速度除 `131` | 同上初始化与 `get_icm20602_gyro_unit` | 明确不一致；新代码用 ±250dps 配 `0x00` 和 `131`，或统一修改两者 |
| 配置 `ACCEL_CONFIG=0x10` (±8g) | 同上 | 新代码 ±2g `0x00`，换算 `16384 LSB/g` |
| 加速度与陀螺仪分两次读，数据保存在全局变量 | 同上 | `0x3B` 起连续14字节，结果入局部结构再发布完整快照 |
| 厂商工程包含 HAL/CMSIS/主函数/异常路由 | RAR 目录 | 不覆盖 RT-Node 的 P1 时钟、SysTick/FreeRTOS/安全入口 |
| 示例未体现明确 `I2C_IF(0x70) Bit6` 置位的 SPI-only 配置 | `Src/SEEKFREE_ICM20602.c` | 官方要求 SPI 使用时禁 I²C；按复位后时序配置，读回关键值 |

注意：`SEEKFREE_ICM20602.c` 中的 GPIO 片选宏经 `main.h` 定义，但 C 文件未直接 `#include "main.h"`；不要机械搬用，按清晰的工程头文件依赖实现。

## 参考优先级

1. ICM20602 官方 DS-000176 Rev 1.1 对寄存器语义、FSYNC、SPI、电源和复位的规定。
2. RT-Node 当前实际 HAL、FreeRTOS、Actuator 和 P1 安全契约。
3. 厂商示例的模块连线命名、初始化思路和寄存器常量（存在已发现不一致，不可无审查复制）。

官方资料：<https://invensense.tdk.com/wp-content/uploads/2020/11/DS-000176-ICM-20602-v1.1.pdf>
