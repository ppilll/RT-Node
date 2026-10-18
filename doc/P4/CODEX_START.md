# Codex App 启动指令（先复制 P4a 部分）

## P4a — 第一个 Codex 对话

你正在 RT-Node 仓库实现 P4a USART1 DMA Circular。开始前阅读 `doc/P4a/README.md`、`design.md`、`interfaces.md`、`implementation_plan.md`、`test_plan.md`、`codex_tasks.md`，同时核对当前 `Core/Src/main.c`、`Core/Src/interrupts.c`、`Core/Inc/FreeRTOSConfig.h`、`Projects/RTnode.uvprojx`、仓库内 UART/DMA HAL 和 P3 控制模块。严格执行 `codex_tasks.md`：最小改动、新增静态 UartTask，DMA1 Channel5 RX circular，IDLE+HT/TC+NDTR，含持续流消费和覆盖检测，行协议 PING/COUNT?，短回复仅 UartTask 发送。不碰 CAN/IMU/Control/TIM3，不引入额外架构或复杂测试。能执行 Keil ARMCC 5.06u7 构建则构建；不可用就明确写未验证，不要伪造。交付代码变更摘要、构建结果、T1 用户实机操作、待确认事项。T1 未由用户确认不得标记 P4a PASS，也不得开工 P4b。

## P4b — 第二个 Codex 对话（仅在 P4a 验收后启动）

基于已验收 P4a 实现 P4b；阅读 `doc/P4b/` 全部六份文档及 P4a 交接。仅增加 ADC1 PA1 + DMA1 Channel1 Circular + TIM2_CH2 内部触发，约 1kHz、12MHz ADC clock、64×uint16 buffer 和低频 UART 报告；扩展既有 UartTask，不新增 AdcTask。注意 STM32F1 ADC1 regular EXTSEL 需要 TIM2_CC2，而非 TIM2_TRGO；TIM2 CH2 内部波形须配置 CC2E+PWM2 以形成实际边沿，**PA1 依旧保持模拟输入，不启用 TIM2 GPIO 输出**。保留 1kHz Tick、ControlTask、ImuTask、CanTask 和 TIM3 配置。实机 PA1/P7 接线未确认，T2/T3 不能虚报结果。提供代码变更、Keil 结果、T2/T3 操作及交接记录；禁止自行进入 P5。
