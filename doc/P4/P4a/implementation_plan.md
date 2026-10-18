# P4a 实施计划（单个 Codex 对话）

1. **核查且只核查必要源码**：`Core/Src/main.c`、`interrupts.c`、`FreeRTOSConfig.h`、HAL conf、`Projects/RTnode.uvprojx`、UART/DMA HAL 头源文件、`App/can/can_app.c` 任务/IRQ、`Driver/imu/icm20602.c`。不要扫描旧 evidence、重复 P1～P3 测试。
2. **新增 UART 模块**：创建 `App/uart/uart_app.h/.c`，静态 UART/DMA handle、256B circular RX、32B 行解析缓冲、UartTask 静态 TCB/384word 栈。硬件 MSP 初始化使能 GPIOA/USART1/DMA1 时钟，注意 DMA1 以后由 ADC 复用，禁止整体重置 DMA1 控制器。
3. **HAL 与 IRQ**：HAL_UART_Init/LinkDMA/Receive_DMA；为 USART1、DMA1_Channel5 设置优先级6；启动顺序避免在调度器之前触发任何 FromISR。IDLE + HT/TC + 5ms 定期读取 NDTR，并实现回绕累计、覆盖检测/恢复、HAL 错误中止后任务态重启。
4. **最小协议**：行终止、分包、多行、最大长度、PING/PONG、COUNT?、ERR；UART TX 由 UartTask 短超时阻塞发送，避免阻塞 ControlTask。
5. **Keil 工程**：仅向 `RTnode.uvprojx` 添加新的 UART `.c`、IncludePath 和当前尚未编译的 HAL `stm32f1xx_hal_uart.c`。`stm32f1xx_hal_conf.h` 中 UART/DMA 模块已启用，不需要重复改变；`FreeRTOSConfig.h` 不改。
6. **main 接入**：在既有四模块 Init 后新增 `UartApp_Init()` 并检查返回值，调用原有 `P1a_FatalError()` 处理初始化失败；不移动/修改既有初始化及 SysTick 交接。
7. **构建**：优先使用现有 Keil5 ARMCC5.06u7 的一个正常构建；若运行环境缺失不能声称通过。仅修正与 P4a 直接有关的编译错误，不做负向构建矩阵。
8. **交付**：简述新增/修改文件、初始化顺序、DMA/TX 行为、实际 Keil 输出；给用户执行 `test_plan.md` 的 T1，标记 `PENDING` 直到收到实测结果。

## 允许修改范围

- 可新增：`App/uart/uart_app.[ch]`。
- 可少量修改：`Core/Src/main.c`、`Core/Src/interrupts.c`（仅 ISR 放这里时）、`Projects/RTnode.uvprojx`。
- 原则上只读：`Core/Inc/FreeRTOSConfig.h`、`Core/Inc/stm32f1xx_hal_conf.h`、`App/control/*`、`App/can/*`、`App/imu/*`、`App/actuator/*`、原有 HAL 驱动源文件。只有发现确凿缺失时先说明原因，不擅自重构。

## Done 的严格定义

代码完成 + Keil 编译真实通过 + 用户 T1 实机 PASS = P4a 验收候选。否则写明 **PENDING/FAIL**，不能把“待测试”填成 PASS。完成后提交 `doc/P4a/P4a_HANDOFF.md`（功能、文件、资源、构建、T1、问题、是否建议 P4b），不生成额外证据树。
