# P2b 需求 V1.0：最小本地跟随

## 输入与硬件

- STM32F103ZET6，FreeRTOS 1kHz Tick；现有 P2a 的 ImuTask（10 tick / priority0）、ControlTask（20 tick / priority1）。
- ICM20602 独立手持，短导线 SPI1：PA5 SCK、PA6 MISO、PA7 MOSI、PC4 CS；VCC=3.3V；其他 P2a 硬件配置不变。
- 舵机由 TIM3/PC6 Pan、PC7 Tilt；Actuator 中位1500µs，Pan硬限制1000~2000，Tilt 1200~1600；P2b 不能扩展边界。
- 因为六轴 IMU 无磁参考，只有 Roll/Pitch 的重力参考；**Pan 轴在本项目中指舵机机械通道，不代表绝对世界航向 Yaw**。

## 功能范围

1. 在 ControlTask 内，定期 `Imu_GetSnapshot` 取得完整快照；检查 READY、valid、年龄和 generation；不访问 SPI。
2. 新增 `CONTROL_SOURCE_LOCAL_IMU`，并明确定义 `CONTROL_LOCAL_FOLLOW` 运行状态；现有 TEST 不得改作 LOCAL。
3. 保留 **一次性授权/terminal**：未按 KEY0、未验证快照、未编译明确测试允许时绝不产生 PWM；KEY1 正常停止并 terminal；故障关闭锁存；无自动重新使能。
4. KEY0/KEY1 方案候选：KEY0=PE4，KEY1=PE3，低有效+内上拉+20ms任务内有限去抖；先在现场核实板卡与引脚复用；不新增按键任务、中断或消息总线。
5. 捕获 KEY0 授权瞬间的 Roll/Pitch 为相对零姿态，增量角 → 绝对 PWM 目标（非积分增量算法）；Pan←Roll、Tilt←Pitch 为初始候选，可在 `p2b_config.h` 独立配置正负方向。
6. 采用小范围、死区、速率限制；只在 ControlTask 内调用 Actuator API。
7. P2a IMU 异常/失效/过期：停止输出、本次会话终止，不读取旧角度继续运动。恢复通信不得重放旧动作。
8. 输出单元无动态分配；P2b 不加新的任务/队列/DMA/UART/GPIO Trace 框架。

## 保持不变的 P1 不变量

- `App/actuator/*` 六 API 及 TIM3 配置与故障锁存保持；`ForceSafeOutput()` 在 fatal 原路径调用、不可运行时清锁存。
- `ControlTask` 唯一正常执行器所有者；20ms 周期，P1 的 DWT/执行时间/迟到保护原则保留。
- 启用后的首自然 PWM 中位帧/≥40tick priming，与驱动 UIF 检查完整保留。
- P1b TEST 原有构建、源检查、回绕/TTL/终止行为不得回归。
- 不改 core 时钟、startup、SysTick/NVIC、FreeRTOS port、调试器假设。

## 非目标

- 不处理永久绝对 Yaw、姿态坐标系自动识别、传感器热插拔重连、外部控制网络、无线、复杂多模式仲裁、速度闭环、PID、EKF、异常分级恢复、大规模故障注入。
