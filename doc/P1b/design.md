# P1b 设计 V1.0-preflight

## 0. 现状事实 vs 设计

远程 `7588842c` 已提交 HAL+TIM3 安全启动、`App/actuator`、P1a RAM mailbox、FreeRTOS kernel v10.4.6及 ARM_CM3 RVDS port。`Core/Inc/FreeRTOSConfig.h` 定义 1kHz tick、静态内存、overflow=2、NVIC 4bit/5阈值；`Core/Src/freertos_static.c` 提供 Idle 静态内存；`Core/Src/interrupts.c` 把 SVC/PendSV 视为异常，需在 RTOS 版本中修订。用户本地有后续自动测试版，远端尚不可见。**下面设计不是实现或板上验证。**

## 1. 启动链和安全优先级

复位 → 最早寄存器级 PC6/PC7 关断 → 更新 SystemCoreClock → HAL 初始化 → HSE 8MHz/PLL 72MHz 和 APB1=36MHz → `Actuator_InitSafe()`（不 Enable）→ 创建静态 ControlTask → `vTaskStartScheduler()` → 默认 DISABLED → 仅在无舵机受控编译测试授权时单次 Enable（先 20ms 低帧，随后中位）→ 按周期 Apply → 结束 Disable/停留安全。任何失败进入不返回关断并锁存。即使调度器未启动，也不允许默认开 PWM。

## 2. FreeRTOS 端口/构建

- 唯一 `Middlewares/FreeRTOS/portable/RVDS/ARM_CM3/port.c` + 本目录 `portmacro.h`，适配 ARM Compiler 5.06u7。禁止同时加入不同 Cortex-M 的 port、多个 heap_x.c。
- 最小编译集合先核对 `tasks.c`、`list.c`、`queue.c` 等真实链接依赖；当没有队列/消息结构时不应无条件加入全部 FreeRTOS 源。若 kernel 需要 queue.c，列明依赖。`freertos_static.c` 唯一 Idle TCB + stack。`xTaskCreateStatic` 为 ControlTask 创建静态 TCB/栈。`configSUPPORT_DYNAMIC_ALLOCATION=0` 保留。
- 链接前审查 CMSIS startup、SystemInit、HAL timebase 是否出现双定义，唯一 Keil target。保留 P1a 测试源码但不参与正常 P1b 启动；清晰的 test macro 互斥检查。
- 诊断栈余量启用 `INCLUDE_uxTaskGetStackHighWaterMark=1` 的最小支持，注意返回单位为 StackType_t（words），结合 map 文件解释字节数。避免启动大量诊断任务。

## 3. 中断、Tick、故障

- 检查现存 `Core/Src/interrupts.c` 的 `SVC_Handler`/`PendSV_Handler` 与 `FreeRTOSConfig.h` 的 `vPortSVCHandler`/`xPortPendSVHandler` 宏。不得保留 P1a 中同名致命 handler 导致 port handlers 冲突。依据当前 RVDS port 具体符号分配唯一 SVC、PendSV、SysTick 定义；不把异常入口默默交由 Startup weak fallback。
- RTOS 启动前的 HAL timebase / 启动后的 FreeRTOS SysTick 由**一个** `SysTick_Handler` 协调；可审查方案是每 tick `HAL_IncTick()`，调度器运行后再 `xPortSysTickHandler()`，或使用独立 HAL timebase，但若更改必须明确原因。`HAL_InitTick` 和 port 初始化不会重复配置相互覆盖 Tick；不允许在 ISR 中 `HAL_Delay`。
- `NVIC_PRIORITYGROUP_4`（4 抢占位，不用子优先级），`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`、lowest=15；从中断调用 FreeRTOS FromISR API 的优先级数值必须 >=5，本阶段尽量没有业务 ISR API。
- `configASSERT` → `AppFatal_Assert` 真正实现，`vApplicationStackOverflowHook` 签名/条件与本地内核匹配，紧急原语硬件先关、故障锁存、不返回。SVC/PendSV 是正常 RTOS 所需异常，不允许当作故障关断。

## 4. ControlTask 和 API 所有权

- 正常 Actuator API 只在 ControlTask 上下文调用；启动主线程只 `InitSafe`，不 Enable/Apply；致命路径允许特殊关闭。
- `vTaskDelayUntil` / `xTaskDelayUntil` 使用本地 kernel v10.4.6 的真实定义，1kHz tick 下间隔 20 ticks。计划起始 wake tick，控制流程应该避免无界串口写/随机延时。
- 最小测试源可以由 ControlTask 在单写者上下文生成 **完整 CommandSnapshot**，数据包含 `source=TEST`, `generation`, `issued_tick`, `ttl_ticks`, `valid`, `pan_us`, `tilt_us`；P1b 不需要提前实现跨任务生产者，接口留给 P2/P3 使用。不要用散落全局数值、单独的 `valid` 更新充当原子快照。
- 只有全量命令快照合法且允许的明确模式 + fresh generation 才可能使能。暂不需要 IMU/CAN 来源。禁止故障复位后旧命令再次通过；runtime fault latch 不由普通 ControlTask 清除。
- 开始测试启用后至少经过第一自然 update event 才 Apply，以当前 P1a 合约为准，不能直接在 Enable 后立刻调用 Apply。
- 一旦 `xTaskDelayUntil` 返回未实际等待、观察到过期/连续迟到或执行预算超限，按评审阈值关闭并记录。RTOS tick 的实际 20ms 不是硬件 PWM 的 20ms: 两者没有锁相，双通道 CCR shadow 生效可能顺延到下一 PWM 周期。

## 5. 无 SWD 测试策略

- 默认生产编译宏 `P1B_AUTOTEST_ENABLE=0`：默认只调度，不使能 PWM。受控无舵机镜像=1：复位后先安全关断并等待固定预启用期，再以有限次数安全中位+小幅目标命令测试，自动 Disable，不循环。
- 使用 PC6/PC7 分别测 PWM 正常工作；但 **PC6/PC7 50Hz 只能验证最终 PWM，不足以验证 ControlTask 唤醒抖动/执行时间**。
- 最低成本的可测量方案：选出开发板实际空闲、不属冻结资源的单独 `TRACE_PIN`，在任务入口/退出输出短脉冲，用逻辑分析仪另一轮单独采集该 pin（如仅两个通道）。**Codex 必须核对板级资料和引脚冲突，先向用户提出候选并获得批准，不能凭空选 PC8/PC9 等脚。** 若用户拒绝新增 pin，只能标识时序验收 NOT_RUN；可提供经用户批准的 CH340 USART1 诊断替代方案，但不能越过 P0 的 USART1 资源冻结自行实现。
- 可用 `DWT->CYCCNT` 补充执行时间统计，但要初始化/判断计数器有效并设定读出途径；只存在 RAM 中的统计变量不算外部可验证，且不能用无 SWD 的 Watch 窗口作唯一证据。
- Trace pulse：一个周期一个上升沿用于测 wake interval，高电平宽度作为某段任务 execution window 的观测值。标记边界、编译优化/中断干扰、时间基准和覆盖次数应记录。Trace 不能破坏执行器安全路径和任务时序。

## 6. 静态内存/故障隔离

- 记录 `StaticTask_t`、ControlTask StackType_t 数组及 Idle stack 在 SRAM 的位置和 MAP。优先最少任务；不要无理由引入队列/软件定时器或 heap。
- 编译栈深度单位 `StackType_t`，非字节；保留安全余量待真实测量。不同构建优化选项影响余量，日志需保留。
- 正常 Disable 非硬件故障，和真正紧急 ForceSafeOutput 的锁存语义分开；绝不通过重新 `Actuator_InitSafe` 清除故障。

## 7. 风险与未决

(1) H10 未记录；(2) 本地自动测试版未上远端；(3) 无 SWD，可用 trace 引脚待批准；(4) 需要真实 ARMCC5 build+map；(5) HAL/FreeRTOS Tick 处理要评审；(6) 故障后所有普通调用权必须关闭。遇资源冲突/安全策略变更先停报总控，不私改冻结条件。
