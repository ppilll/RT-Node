# P4b — ADC1_IN1 + TIM2_CC2 + DMA Circular

**启动条件**：P4a 的代码与 Keil 正常编译完成，用户通过 T1 实机，并有 `doc/P4a/P4a_HANDOFF.md`。P4b 在 P4a 的既有 UART 接口上增量实施。未达到条件时禁止在同一分支上同时改共享文件。

阶段目标：PA1/ADC1_IN1 单通道12-bit，TIM2_CH2 内部硬件周期触发约1kHz，DMA1 Channel1 circular收集样本，32样本平均，串口 `ADC?` + 每200ms低频报告。与 CAN/IMU/Control 任务并发，保持原执行器安全逻辑。

**先读顺序**：本页 → `design.md` → `interfaces.md` → `implementation_plan.md` → `test_plan.md` → `codex_tasks.md`，并阅读 P4a 已验收代码及交接。

关键已知资源：HCLK=72MHz、PCLK1=36MHz（TIM2输入72MHz）、PCLK2=72MHz。TIM3 已用于 PWM 且直接寄存器管理，绝不修改。PA7 被 ICM20602 SPI1 MOSI 占用，P4b 使用 **PA1**，无冲突。P7 是开发板的物理多功能接口而不是 GPIO PA7。P7/STM_ADC/TPAD/RV1 实际跳线仍待 T2 前确认。

硬件不可用条件：用户没有 ST-Link、J-Link、SWD、示波器或万用表。仅使用 CH340 串口、逻辑分析仪和安全已确认的板载模拟接线。不报告未测量的电压精度。

P4b 只需 T2 ADC 功能与 T3 四功能并发，共两组；加上 P4a T1，总计仅三组。实际结果出现之前不得标记 P4b PASS 或 P4 ACCEPTED。
