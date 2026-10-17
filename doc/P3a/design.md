# P3a 最小技术设计（V1.0）

## 1. 源码核查（基线 commit `59e4a0b0`）

- `App/control/control.c` 的任务周期=20 tick；P2b 路径受 `P2B_OUTPUT_STAGE>0` 控制，默认 stage0 不会调用 Enable/Apply；ControlTask 的 DWT deadline 和故障处理保留。
- `App/control/control_policy.c` 的 `ControlSession_Close()` 强制 terminal；P3a **不修改**，P3b 才评审正常 rearm API。
- `App/imu/src/imu.c` 使用 ICM20602/SPI1、10ms 采样、静态完整快照；不碰 IMU。
- `App/actuator/src/actuator.c` 的 TIM3 PC6/PC7、Disable/ForceSafeOutput、最终限幅保持原样。
- 仓库已有 `Driver/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_can.c`、头文件，`HAL_CAN_MODULE_ENABLED` 已启用；**Keil uvprojx 没将 hal_can.c 加入源文件组**。
- `Middlewares/FreeRTOS/queue.c` 文件存在，但 **Keil uvprojx 未列入**；若采用静态 Queue，必须增加此文件。
- 现有 `interrupts.c` 包含 SysTick 和 fatal exception handlers，无 CAN IRQ 强定义；startup 已定义 CAN1 相关中断向量槽。
- `FreeRTOSConfig.h`：静态分配、1kHz 32bit tick、最大可调用 FreeRTOS API 的 NVIC 优先级数值5、ControlTask priority1、ImuTask priority0。

## 2. HAL CAN 配置

| 项目 | 设计 |
|---|---|
| 外设 | CAN1 bxCAN，APB1=36MHz |
| 引脚 | PA11=CAN_RX 输入，PA12=CAN_TX 复用推挽；默认映射；不使用 CAN remap |
| 过滤 | FIFO0，单个32bit掩码过滤标准数据 ID 0x180..0x183（base 0x180 / mask 0x7FC），IDE/RTR 位必须匹配标准数据帧 |
| CAN模式 | Normal（不要永久 loopback/silent）；Classical CAN |
| 位时序 | **历史实测值待确认**；Prescaler/BS1/BS2/SJW 经 APB1=36MHz 计算，且对端完全一致；禁止猜值 |
| 自动重传 | 开启 HAL AutoRetransmission（如当前 HAL 字段支持）；TX non-blocking mailbox，满则跳过本次状态发送 |
| Bus-Off | 推荐 HAL AutoBusOff=ENABLE 并通过 CAN1_SCE/error callback 标记状态；不加重型重试/诊断框架 |
| Rx FIFO | FIFO0 interrupt，最短 ISR 提取完整帧+本地 tick，投递静态 queue |
| NVIC | 优先级分组4，CAN1 RX0/SCE 抢占优先级数值6，可合法使用 `...FromISR()` |
| TX | `HAL_CAN_AddTxMessage()`，不阻塞 ControlTask，状态帧可合并或丢弃，100ms 下次再试 |

注意 PA11/PA12 与 USB 复用，不允许同时配置 USB。MCU PA11/PA12 为数字 CAN TX/RX，必须经过实际 CAN 收发器接入差分 CANH/CANL；终端电阻及共地按历史实测硬件核实。普通逻辑分析仪应测 CAN_TX / CAN_RX 或 PWM PC6/PC7，不直接测 CANH/CANL 的差分波形。

## 3. 上电初始化顺序

1. 原 `main()` 保留 P1a_ForceSafeOutput、HAL、时钟、Actuator_InitSafe、Control_Init、Imu_Init。
2. 调用 `CanApp_Init()`：创建静态队列和 CanTask、HAL_CAN_Init、ConfigFilter、NVIC/MSP 配置。此时**不启用 CAN 接收通知，不允许 CAN ISR 使用未启动的 FreeRTOS**。
3. 在 `vTaskStartScheduler()` 后，由 CanTask 调用 `HAL_CAN_Start`、`HAL_CAN_ActivateNotification`。
4. CAN 发送 STATUS 周期100ms。CAN 启动失败记录为通信不可用；P3a 执行器仍 disabled，不因为 CAN 错误主动使能，也不修改 Actuator 层。
5. `CanApp_Init()` 启动前致命初始化返回 false 交回原 `P1a_FatalError()` 路径，既有 fail-closed 语义不改变。

## 4. 收发任务

- FIFO0 Rx callback 最多排空硬件待收帧（硬件深度受限），每帧立即抓取 xTaskGetTickCountFromISR；保持 ISR 短且不进行模式/协议长流程。静态 Queue 深度8，元素为 `StdId/IDE/RTR/DLC/payload[8]/received_tick` 的固定结构（不需动态分配）。
- Queue 满记录单一 overflow 状态位；P3a 将其报告在 STATUS reason，不能编造已成功处理的帧。P3b 才进一步实现溢出时安全 Disable。
- CanTask 和 ImuTask 同为优先级0，ControlTask 优先级1保持不变；CanTask 阻塞式等待 queue，定时处理 STATUS，避免忙轮询。不得为了 CAN 调整 DWT 的2ms控制预算而不测量。
- CanTask 解析协议，heartbeat 新计数更新本地时刻；合法 MODE_REQUEST 在 P3a 一律返回拒绝状态；REMOTE_STOP 合法则幂等确认；REMOTE_CONTROL 仅解析后丢弃，无控制命令发布。
- CanTask 生成 STATUS，正常状态=DISABLED、epoch=0、actuator_enabled=0；已有致命故障必须报告 FAULT，不得伪报 DISABLED；IMU readiness 用已有 Imu_GetSnapshot 判定 `READY/valid/sample_age<40tick`；Bus-Off 取实际 CAN 状态；heart bit2 按本地接收计时。
- 在任务中发 STATUS；HAL 发送调用不会阻塞等待邮箱，出错无需新增复杂 TX 队列。若 Bus-Off 暂时无法 TX，应在恢复后继续下一轮状态发送。

## 5. 边界与不做的变更

- P3a 不增加 `REMOTE_CAN` 枚举、不触碰 ControlSession、不建立 P3b mailbox；接口布局已冻结，P3b 再实施。
- 保留现有 Actuator HAL/寄存器实现，尤其 `AFIO->MAPR` 的特殊处理，不用 CAN remap 覆盖 TIM3 配置。
- 原 `interrupts.c` 只在必要时最小编辑；首选让新 CAN 模块提供强 IRQ 函数定义，从而避免修改现有异常路径。
- 不改变 P2b 授权宏、KEY0/KEY1、LocalFollow、本地姿态采样或历史测试行为。
