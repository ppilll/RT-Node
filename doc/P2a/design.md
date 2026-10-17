# P2a 详细设计（最小必要实现）

## 1. 模块分层与所有权

数据路径：`ICM20602` → `SPI1 / PC4` → `icm20602_driver` → `ImuTask` → `imu_fusion` → `imu_snapshot`。

建议新文件（具体目录可按现有风格调整，但不得修改 P1 执行器模块）：

```text
App/imu/include/imu.h                  # Imu_Init、状态/快照公共 API
App/imu/include/imu_types.h            # uint32 时间戳、状态、量纲结构
App/imu/src/imu.c                      # 静态 ImuTask、校准、发布
App/imu/include/imu_fusion.h           # 与 HAL/RTOS 无关
App/imu/src/imu_fusion.c               # 纯 C 互补滤波
Driver/imu/icm20602.h                  # 芯片常量/采集 API
Driver/imu/icm20602.c                  # HAL SPI / CS，返回明确错误
```

若现有目录命名适合更少源文件，可以合并同层文件；最终 `interfaces.md` 的公共 API/单位/所有权不得变化。不要引入 framework/config registry/多态接口。

## 2. SPI1 初始化

- PCLK2=72MHz、SPI1 PRESCALER=128 ⇒ 562.5kHz。HAL 8BIT/MSB，`SPI_POLARITY_HIGH`、`SPI_PHASE_2EDGE`、`SPI_NSS_SOFT`；禁止 SPI1 AF 重映射。
- 打开 GPIOA/GPIOC/SPI1 的必要时钟；PC4 先设置输出锁存高，再配置推挽输出；PA5/PA7 设适合 STM32F1 的 AF 推挽输出，PA6 设输入。若调用 `HAL_SPI_Init()`，需在明确归属的 MSP 函数中配置 GPIO；避免重复定义厂商 `HAL_SPI_MspInit` 或覆盖全局回调。所有新 GPIO 初始化不得篡改 `AFIO->MAPR`，P1 对该寄存器的完整 JTAG/TIM3 配置必须保持。
- 实现 `read_reg(reg,&v)`、`write_reg(reg,val)`、`read_burst(start,buf,n)`；每次先 CS LOW，完整 SPI transfer，最后 **不论成功、超时、错误都 CS HIGH**。SPI 数据读命令 `reg|0x80`（例如 `0xF5`、`0xBB`），写命令 `reg&0x7F`。
- 3ms 每事务 HAL timeout 为候选，Codex 应基于实际 `stm32f1xx_hal_spi.c` 的阻塞/超时行为检查；不允许 500ms 或无限 wait。不要在 ControlTask 或 ISR 调 SPI。
- 14 字节连续读共 15 字节线路通信，在562500 bit/s下理论213us；这是参考量，不等于实际 WCET。

## 3. 初始化顺序（有限状态，不要复杂状态机框架）

建议阶段：`POWER_WAIT → PROBE → RESET_WAIT → SPI_ONLY → CONFIG → VERIFY → CALIBRATE → READY`；失败为 `FAILED`，不在 P2a 自动重启。

- 保证 CS 上电空闲高，按 datasheet 规定的寄存器读写启动时间后再通信。
- SPI 首次 ID 读取 `WHO_AM_I 0x75`，期待 `0x12`。最多 2 次尝试，每次必须结束 CS，最后失败就设 FAILED。
- 对芯片软件复位 `PWR_MGMT_1=0x80`，在**有截止时间**的等待后探测完成；禁止厂商的 `do...while(PWR_MGMT_1 != 0x41)` 无界轮询。可在任务周期唤醒时逐步推进，不必额外任务或系统时钟阻塞。
- 按官方要求在寄存器可用且复位完成后设置 `I2C_IF(0x70)` 的 `I2C_IF_DIS` Bit6=1，以固定 SPI-only；若复位使其清零，则复位后重设。
- 配 `PWR_MGMT_1=0x01`、`PWR_MGMT_2=0x00`、`CONFIG=0x03`、`SMPLRT_DIV=9`、`GYRO_CONFIG=0x00`、`ACCEL_CONFIG=0x00`、`ACCEL_CONFIG2=0x03`；不要不经确认复制厂商的量程/分频值。查阅官方对应位域、时钟源和 DLPF 时间；关键配置读回与写入一致后 READY/CALIBRATE。
- 初始化总等待受限（首版建议不超过 2s，加上静止校准另计）；任何等待读取必须通过 RTOS 可调度方式进行。工程 `INCLUDE_vTaskDelay=0`，不能不加检查直接使用 `vTaskDelay`；推荐在每次 `xTaskDelayUntil` 周期唤醒时推进 1 步并检查 tick 截止。
- 失败不调用全局 fatal，也不改原驱动故障锁存，P2a 发布 invalid/FAILED 并保持输出禁用。

## 4. 样本解码与状态

- 连续读 `ACCEL_XOUT_H(0x3B)` 后 14 bytes，顺序 Ax Ay Az Temp Gx Gy Gz，各 16 位大端二补码；正确处理负数，不能以无符号拼接后直接当正数使用。
- 加速度：`accel_g = raw / 16384.0f`；角速度：`gyro_dps = raw / 131.0f`；原始温度可保存供诊断，但 P2 不需要温度补偿系统。
- 把当前硬件读错、断线异常和 HAL 超时归为简单统一 IO_ERROR/FAILED；不做复杂 SPI NACK 分类（SPI 没有 I²C ACK）。
- 设备静止不会变化很快，**禁止**以连续帧相同、某轴原始零值直接判定故障；可拒绝明显不合理加速度模长或 float 非有限值。

## 5. 姿态解算（纯 C）

- 初始静止校准：建议 100 帧（约1s）平均陀螺仪零偏。采样期间检查重力模长大致在 0.8..1.2g 和角速度是否接近静止（候选每轴低于5dps）；不满足不自动使用错误偏置，限定最长校准时间（建议3s，失败 FAILED）。具体阈值作为编译常量并在测试报告注明，不追求精密温补。
- 加速度倾角（芯片轴系）：`roll_acc = atan2f(ay,az)*RAD_TO_DEG`，`pitch_acc = atan2f(-ax,sqrtf(ay*ay+az*az))*RAD_TO_DEG`。静止倾斜时检查符号。陀螺仪 X/Y 在小姿态范围分别积分修正 Roll/Pitch；**首版面向小角度手持输入**，不宣称大角度三维严格姿态精度。
- 互补滤波：`alpha = tau/(tau+dt)`，`roll = alpha*(roll + (gx-bx)*dt) + (1-alpha)*roll_acc`，pitch 同理；`tau` 初候选0.5s。陀螺初值在校准结束采用加速度倾角；无磁力计时不输出绝对 Yaw。
- `dt = (current_sample_tick - previous_sample_tick)/1000.0f`，可处理 32-bit tick 模数差；首次样本/异常间隔（如 0ms 或>50ms）不作正常积分，置 invalid 或滤波重初始化，选择一种简单、可测试的固定策略。不要把 10ms 恒定代入算法；在 Cortex-M3 上没有 FPU，不声称软件 `atan2f/sqrtf` 零成本。
- 仅在数据有效、校准完成、估计为有限数时发布 `valid=true`；失败更新状态而不让旧姿态继续有效。

## 6. ImuTask 调度与中断保护

- 仅静态任务：`xTaskCreateStatic`，`Imu_Init()` 由 `main()` 在 `Actuator_InitSafe()` 和 `Control_Init()` 之后、scheduler 之前调用；`Imu_Init` 只创建任务，不能主动 Enable 舵机。
- **P2a 初始优先级 0（与 Idle 相同）**，ControlTask 保留 priority1，IMU 单个事务阻塞不能抢占 ControlTask；ImuTask 自己用周期唤醒时戳、真实 dt 处理有限采样抖动。不要为了追求理论上的严格100Hz先调整 P1 优先级；如实机发现不可用，先报告再批准改调度。
- 初始静态栈候选 512 words（2048bytes），最终值由编译 MAP/可见的运行测试评审；默认最小资源优化可根据证据修改。注意 STM32F103 SRAM 64KB，无 FPU。
- 共享完整快照由 ImuTask 单写，`Imu_GetSnapshot` 在任务上下文用短 `taskENTER_CRITICAL` 完整拷贝；不可 `volatile` 分字段跨任务访问。不能在中断里调用带任务上下文限制的 API。
- 若 ImuTask 遇到错误，不在 100Hz 循环无界重试；最多简短有界处理然后 FAILED，保持 P2a 执行器断开。

## 7. 资源/调试约束

- 使用已有 `Core/Inc/stm32f1xx_hal_conf.h` 的 `HAL_SPI_MODULE_ENABLED`；把已有 `Driver/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c` 加入 `.uvprojx`，不要导入厂商 HAL 版本。
- 不改 `Core/Src/interrupts.c`、`Core/Src/rtos_hooks.c`、P1 `actuator`、`control_policy` 或 FreeRTOS port；任何无法完成的编译需求需先报告冲突。
- 无 SWD，日志/内部静态诊断不可直接视为外部证据；具体最小测试见 `test_plan.md`。
