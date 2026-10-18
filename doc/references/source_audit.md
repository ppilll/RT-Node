# P5 源码只读审查基准（2026-10-18）

仓库：`https://github.com/ppilll/RT-Node`  
P5：`fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf`  
以下为已通过 GitHub 文件读取核对的事实；Codex 应在真正修改前重新查看最新 HEAD。

| 文件 | 当前实有能力 / 重要位置 |
|---|---|
| `Core/Src/main.c` | `main()` 最先调用 `P1a_ForceSafeOutput()`；HAL/时钟/Actuator/Control/IMU/CAN/UART 初始化，ADC/ENV/Display 非关键初始化；最后共享 SysTick 切换、启动 RTOS。尚无 IWDG 启动或 RCC 重启原因保存。|
| `Core/Src/interrupts.c` | SysTick 先 `HAL_IncTick()`，调度后调用 `xPortSysTickHandler()`；NMI/HardFault/MemManage/BusFault/UsageFault/DebugMon 进入 `P1a_FatalError()`。|
| `Core/Src/rtos_hooks.c` | `AppFatal_Assert` / StackOverflowHook -> `P1a_FatalError`。|
| `App/control/control.c` | ControlTask 20 ms prio1，`wake_count` 每轮入口更新；`GetDiagnostics` 快照；启动 `boot_local_pending=true`；KEY0 锁存；`P3bCycle` 先 STOP/FAULT 处理、再 MODE / Motion。独占正常 PWM。|
| `App/control/control.h` | `ControlDiagnostics.wake_count`, `Control_GetStatus()` 的 `state/epoch/key0_stop_latched/enabled` 可直接读取。|
| `App/control/control_policy.c` | session source/generation/TTL/范围/terminal，不能用 Health 模块替代。|
| `App/control/control_config.h` | 20 ms, 100 ms TTL, 2 ms 执行预算，256-word 控制栈。|
| `App/control/p3b_config.h` | `P3B_OUTPUT_STAGE` 默认 0、1 为无舵机电源/信号的 LA 测试、2 必须额外批准。|
| `App/actuator/src/actuator.c` | `Actuator_Disable`、`Actuator_ForceSafeOutput` 和驱动故障锁存已存在，不允许重复开发。|
| `App/imu/src/imu.c` | `ImuTask` 静态 prio0，10 ms；FAILED 后不再做 SPI；snapshot 有 state/valid/generation/tick。|
| `App/can/can_app.c` | 静态 CanTask prio0；CAN ID 0x180/0x181/0x182/0x183/0x280；STATUS DLC8 已冻结；中断溢出由 ControlTask 消费，不能新增协议字节。|
| `App/uart/uart_app.c` | USART1 DMA Circular + UartTask prio0，115200；`ParseByte()` 当前支持 `PING`, `ADC?`, `ENV?`, `COUNT?`，行上限 31 chars，64B reply，`SendReply()` 小块阻塞发送；单一 UART TX 所有者。|
| `App/adc/adc_app.c` | ADC DMA 状态 INIT/START/DMA/STALLED，`AdcApp_Process()` 由 UartTask 运行。|
| `App/env/env_app.c` | SensorTask 500 ms，`valid`/sample_tick 快照，错误只影响 ENV。|
| `App/display/display_app.c` | DisplayTask 5 ms step；KEY1 PE3 只翻页；OLED 故障 local，仍检查按键；显示健康状态 `healthy` 目前为局部变量，不能用其他 getter 的 `valid` 冒充 OLED 健康。|
| `Core/Inc/FreeRTOSConfig.h` | 静态分配且禁动态，1kHz tick, configUSE_TIMERS=0, INCLUDE_xTaskDelayUntil=1; Control prio1, 其余优先级0。|
| `Core/Inc/stm32f1xx_hal_conf.h` | `HAL_IWDG_MODULE_ENABLED` 当前开启，`LSI_VALUE=40000` 为典型值。|
| `Projects/RTnode.uvprojx` | Keil IROM 起始 0x08000000、Size 0x80000；尚未编入 HAL IWDG 和 HAL FLASH `.c` 实现；P6需修改源码组、头文件包含路径和 IROM。|
| `Driver/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_iwdg.c` | 已存驱动；其 `HAL_IWDG_Init` **会启动**硬件，使用 `HAL_GetTick()` 等待状态标志；不能把调用看作无副作用。|
| `Driver/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c` | 已存，未加入 Keil 构建；优先复用 HAL_FLASH_Unlock/Program/Lock 及读回校验。|

复位标志可能多位同时存在：以 IWDG 作为安全优先事件，保存 raw flag；不要假设 PINRSTF 单独表示用户按键。复位标志按硬件规则及时清除。
