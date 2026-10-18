# P4a — USART1 RX DMA Circular

阶段目标：使用 USART1 PA9/PA10 经 CH340 进行 115200 8N1 通信，DMA1 Channel5 循环接收，支持空闲线与连续流、跨环形边界、简单行协议，静态 FreeRTOS UartTask 输出 PONG 和计数。仅 T1 一组实机测试。

**先读顺序**：本页 → `design.md` → `interfaces.md` → `implementation_plan.md` → `test_plan.md` → `codex_tasks.md`。

已核查工程：

- 主分支 P3b `7bb967a3b8c47b63bd4afdc44feabba0c4e4bf00`。`main.c` 在 scheduler 前依次 `Actuator_InitSafe/Control_Init/Imu_Init/CanApp_Init`；不移动上述顺序。
- `Core/Inc/FreeRTOSConfig.h`：静态分配、`configUSE_TASK_NOTIFICATIONS=1`、无 FreeRTOS 软件定时器，SysTick=1 kHz，NVIC priority grouping 4，`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`。
- `App/control/control.c`：ControlTask prio1；`App/imu/src/imu.c`：ImuTask prio0；`App/can/can_app.c`：CanTask prio0、CAN IRQ prio6。
- Keil 工程已经编译 HAL DMA / SPI / CAN；**尚未将** `stm32f1xx_hal_uart.c` **列入** `RTnode.uvprojx`。HAL_UART_MODULE_ENABLED 和 HAL_DMA_MODULE_ENABLED 已开启。
- 仓库旧 HAL 具有 `HAL_UART_Receive_DMA`、Rx HT/TC 弱回调、`HAL_UART_IRQHandler`，没有 `HAL_UARTEx_ReceiveToIdle_DMA`；**禁止照搬更新版 HAL API**。

串口硬件：用户已确认手动 BOOT0+RESET 下载，用户程序启动后 CH340 可串口收发。将 BOOT0 拉回 0 并 RESET 才可测试应用；下载软件和串口助手不要同时占用 COM。

边界：不接入云台控制、不改 CAN 协议、不重构 P1–P3、不重做其验收、不引入测试授权码和额外日志框架。P4a 完成后待用户提供 T1 实测，再生成 `P4a_HANDOFF.md`；未通过不得继续 P4b。
