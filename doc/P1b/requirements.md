# P1b 需求与边界（v1.0）

## 前置 Gate

1. P1a 编译 + H01–H09 无负载测试（用户报告 Case1–3 PASS，不等价覆盖所有子项；无数值记录处标注）。
2. P1a H10 实机受限机械验证完成或明确暂不执行 P1b 的一切带载验证；**正式 P1b 编码与 Gate 以 P1a ACCEPTED 为前提**。Codex C0 核对记录，不用重复无故的已有测试。
3. 使用唯一 Keil MDK5 工程/ARMCC5；若本地项目/测试宏与远端不同，保护可运行版本、记录 hash 和 diff。

## 必须实现

- FreeRTOS kernel v10.4.6，Cortex-M3/RVDS ARM_CM3 port 唯一；所有运行时任务对象静态分配；禁止未经设计使用动态 heap。
- `ControlTask` 为唯一正常调用 `Actuator_Enable/Apply/Disable` 的任务；紧急硬件关闭允许在批准的致命异常路径绕过，但该路径必须不返回或确保软件状态锁存一致。
- 目标任务周期 20 ms（`configTICK_RATE_HZ=1000`），启动默认禁止输出；显式受控测试授权前无 PWM。
- `ControlTask` 不等待任何 I2C/SPI/CAN/UART，不在 ISR 调用普通 Actuator API。
- 控制命令采用完整快照（至少 source、generation、valid、target pulses、timestamp/expiry）并明确唯一写者；未来模式切换和故障清除后，旧生成号/命令无效。当前只实现最小本地测试源；不提前实现完整控制仲裁框架。
- HAL/RTOS Tick 配合、SVC/PendSV/SysTick 向量单一拥有者、NVIC priority group 和 FromISR 优先级规则、静态 Idle TCB/stack、断言与 Stack Overflow hook。
- 保存编译构建证据、MAP、RAM/Flash、任务栈余量、任务周期和执行时间真实测量；不能凭 PWM=50Hz 推导 ControlTask=20ms。
- 软件关闭 PWM 不等于机械制动、安全认证或断电。

## 明确不做

MPU6050、CAN、USART1 DMA、ADC1 DMA、BME280、OLED、完整状态机、完整 watchdog、多外设压力测试、外环 PID、旧 VisionArm 业务迁移、不必要任务/队列/中间件。不要修改冻结 PWM 引脚及驱动既有通过的功能。

## 无调试器环境约束

用户有 CH340 + 逻辑分析仪，无 ST-Link/J-Link；不能要求 SWD Watch、RAM Mailbox、DWT 主机读取作为唯一验收手段。P1b 测试固件可通过受控编译宏一次性上电测试，但默认固件永远关闭执行器。周期抖动/执行时间应设计**可外部观测**的测试路径；优先经用户批准的无冲突 GPIO pulse trace，或者另行批准的临时诊断通道，不擅占 USART1 等资源。DWT_CYCCNT 可作内部辅助，必须通过可靠的外部证据输出才可作验收依据。

## 故障及安全边界

- 默认 `Actuator_InitSafe()` 后 disabled。构建宏不应在正式固件自动开启输出。
- 任何异常/stack overflow/assert/控制超时/非法模式/命令失效/周期超限按所审查策略禁止 PWM、锁存或等待新鲜命令，不擅自清 latch。
- 当前 P1a `Actuator_Enable()` 会在首次自然 UEV 前拒绝 Apply，不能在 task 启动时连发 Enable/Apply；正确等待至少一个完整周期并实测首次启动低帧。
- 复位后自动测试只能在无舵机时运行；H10/带载测试必须使用受限人工授权方案，不用复位即自动扫动做机械验证。

## 初始验收候选（评审前固定）

目标 20ms；建议基于独立任务标记观测至少 1000 连续周期；最小任务负载下任务间隔暂定 19–21ms，最坏测得任务执行时间暂定 <2ms，连续未触发可观察 deadline miss。以上是**候选**，Codex 必须于试验前明确时间基准、仪器能力、测试负载和是否适合目标；发现不合理必须先向用户报告，不得测试后倒改。Flash/RAM、栈余量保留实测，不能先填数字。故障关断时延需提出测量方法/事先阈值，不得凭经验声称满足。
