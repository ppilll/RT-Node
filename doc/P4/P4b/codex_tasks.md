# Codex 任务书 — P4b（仅在P4a真实验收后执行）

你是 RT-Node F103 项目 P4b 实现者。先确认 P4a 已有交接和 T1 实际 PASS，且当前仓库没有别的 Codex 对话并行修改公共文件。

## 目标

实现 STM32F103ZET6 ADC1_IN1(PA1)、TIM2_CC2 内部触发1kHz、ADC12bit 12MHz、DMA1 Channel1 Circular 64×uint16 halfword、32样本平均、电压换算（假设VDDA=3.3V），复用已有 UartTask 输出每200ms报告并支持 `ADC?`。禁止改用ADC软件定时触发或TIM2_TRGO；禁止动PA7 SPI1、TIM3 PWM、CAN/IMU/Control、STOP。

## 必须读

- `doc/P4b/README.md`、`design.md`、`interfaces.md`、`implementation_plan.md`、`test_plan.md`；`doc/P4a/P4a_HANDOFF.md`、已实现UART模块。
- `Core/Src/main.c`、`Projects/RTnode.uvprojx`、`Core/Inc/FreeRTOSConfig.h`、`stm32f1xx_hal_adc.[ch]`、`stm32f1xx_hal_adc_ex.[ch]`、`stm32f1xx_hal_dma.[ch]`、`stm32f1xx_hal_rcc_ex.h`，并核查现有 HAL API 和 TIM2/ADC/DMA寄存器。

## 核心实现约束

1. 新增 `App/adc/adc_app.[ch]`、仅扩展 UART 模块和Keil工程/初始化入口。任务仍然只有 P4a UartTask，没有新AdcTask。
2. TIM2 clock72MHz → PSC71/ARR999/CCR2=500；CH2用内部 PWM2 CC2E 启动产生有效上升沿，PA1保持GPIO模拟输入、不把CH2接物理引脚、不碰AFIO remap/TIM3。ADC regular EXTSEL=`ADC_EXTERNALTRIGCONV_T2_CC2`、ContinuousConv DISABLE，ADC clock/PCLK2=72/6=12MHz；先校准、配置DMA再启动TIM2。
3. HAL_ADC_Start_DMA接口接受`uint32_t*`，但DMA目标是`uint16_t[64]`且内存/外设均16bit。ISR只标记完整半窗，UartTask安全处理后发布raw、avg、mv、n，丢掉过期半窗而不是读取DMA正在改写的数据。
4. USART TX唯一归UartTask。`ADC?`初期若无有效样本返回NOT READY，输出实际测得raw/avg和从**假设**VDDA=3300mV计算的mv，绝对精度未经验证。自动报告200ms周期、不可拖慢ControlTask。
5. 中断优先级6，保留旧有静态FreeRTOS、1kHz Tick与CAN/IMU调度，不加总线、日志、软定时器、测试码或新构建开关。

## 文件变更边界

允许新增 `App/adc/*`；只可增量改 `App/uart/*`、`Core/Src/main.c`、可选 `Core/Src/interrupts.c`、`Projects/RTnode.uvprojx`。添加仓库现有 HAL ADC + ADCEx .c；不要覆盖HAL驱动。不修改 `App/control`、`App/can`、`App/imu`、`App/actuator` 或 `Core/Inc/FreeRTOSConfig.h`。

## 验证/交付

一次正常 Keil5 ARMCC5.06u7 构建，能跑则汇报实际通过/失败，不能跑则PENDING。用户仅用CH340和逻辑分析仪完成 `test_plan.md` 的T2/T3；PA1/P7接线未经确认前禁止带未知电压测试。说明全部改动文件、DMA通道/定时器/ADC触发配置、样本计算/缓冲区所有权、构建/T2/T3结果、待解决问题。用户未提交真实结果不能给PASS。完成后编写精简 `doc/P4b/P4b_HANDOFF.md`；若P4a、P4b都是真实PASS，再编写 `doc/P04_HANDOFF.md` 供独立总控决定P4 ACCEPTED与是否启动P5。
