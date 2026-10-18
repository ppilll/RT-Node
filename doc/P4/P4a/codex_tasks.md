# Codex 任务书 — P4a（可直接执行）

你是 RT-Node F103 项目 P4a 实现者。**当前任务仅 P4a，不得预先开发 P4b**。

## 任务目标

在现有工程中最小改动实现 USART1 PA9/PA10 115200 8N1、DMA1_Channel5 RX Circular 256B、USART IDLE/HT/TC/NDTR 连续流处理、32B ASCII 行解析、`PING → PONG`、`COUNT? → RX/OVF`。新建一个静态 UartTask（优先级0、初始栈384 words），由它独占 UART TX。保持原有 FreeRTOS、CAN、IMU、ControlTask、TIM3、STOP 完全不变。

## 读文件

1. `doc/P4a/README.md`、`design.md`、`interfaces.md`、`implementation_plan.md`、`test_plan.md`。
2. `Core/Src/main.c`、`Core/Src/interrupts.c`、`Core/Inc/FreeRTOSConfig.h`、`Core/Inc/stm32f1xx_hal_conf.h`。
3. `Projects/RTnode.uvprojx`。
4. 仓库自带 `stm32f1xx_hal_uart.[ch]`、`stm32f1xx_hal_dma.[ch]`，参考 `App/can/can_app.c`、`Driver/imu/icm20602.c` 的任务及 MSP 约束。

## 改动范围与技术红线

仅新增 `App/uart/*`，并少量调整 `main.c`、可选 `interrupts.c`、Keil `.uvprojx`。不覆盖 HAL 源码，不添加第三方库或协议框架，不修改舵机 PWM/引脚限幅/CAN 协议/IMU 算法。不同模块的 IRQ 强定义只能出现一次。

检查旧 HAL：此仓库 **没有 `HAL_UARTEx_ReceiveToIdle_DMA`**；使用 `HAL_UART_Receive_DMA` + circular，IDLE 自行清标志、DMA HT/TC + NDTR + 周期检查。明确处理 DMA 已回绕但 TC callback pending 的计数竞态，不能把模指针相同误判为空。ISR 保持短小，且 HAL UART DMA 错误中止后只能在任务态重启。至少一个受控发送路径，不能在 ControlTask 阻塞。

## 必要验证与交付

一次正常 Keil5 ARMCC 5.06u7 编译（可用时）；如运行环境不可用，直接记录未执行，不编造。让用户只做 `test_plan.md` 的 T1，用户没有在线调试器。输出：

- 新增与修改的具体文件；
- DMA 环形回绕/溢出机制和 ISR 优先级；
- Keil 成功或失败的真实摘要；
- T1 操作指导和状态（缺实测则 PENDING）；
- 完成后生成简明 `doc/P4a/P4a_HANDOFF.md`，列未解决问题，给总控建议是否进入 P4b。

不自动启动 P4b，不进入 P5，不进行无关测试与证据体系建设。
