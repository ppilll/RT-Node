# P4b 实施计划（第二个 Codex 对话）

**先决条件**：P4a Keil构建成功且用户T1 PASS，取得 `doc/P4a/P4a_HANDOFF.md`；不得和P4a对话并发修改main/uvprojx等文件。

1. 读取 `doc/P4b/`六文件、P4a已完成代码/交接和当前 `Core/Src/main.c`、Keil工程、`stm32f1xx_hal_adc.[ch]`、`stm32f1xx_hal_adc_ex.[ch]`、`stm32f1xx_hal_dma.[ch]`、`stm32f1xx_hal_rcc_ex.h`、`Core/Inc/FreeRTOSConfig.h`，核对TIM2、DMA Channel1和PA1尚未占用。
2. 新增 `App/adc/adc_app.h/.c`：ADC1_IN1、12MHz ADC分频、CAL、DMA Ch1 64×uint16 halfword circular、HT/TC简短回调、TIM2_CH2硬件触发（PSC71/ARR999/CCR2 500，PWM2内部通道信号 CC2E=1，**GPIO PA1 analog**，非 GPIO AF）。
3. 严格启动顺序：正确初始化时钟和GPIO → ADC初始化及通道配置 → ADC校准 → 启动DMA等待触发 → 启动TIM2计数。ADC regular连续转换必须禁用。不要启动 TIM2 的1kHz ISR。若直接使用TIM2寄存器，避免额外编译 HAL TIM .c。
4. DMA完成半窗提交给低优先级任务 `AdcApp_Process`：样本数32平均、raw、假设VDDA的mv、丢窗轻量计数。要避免读正在被DMA覆盖的窗口；ISR不做打印/滤波循环。
5. 最小扩展P4a UartTask：5ms已有周期顺带 `AdcApp_Process`，每200ms自发输出一条简短ADC行，支持 `ADC?` 读取快照；TX仍由UART任务唯一负责。不增加AdcTask。
6. Keil `.uvprojx` 仅新增ADC app和仓库现有的 `stm32f1xx_hal_adc.c`、`stm32f1xx_hal_adc_ex.c`；添加 include path。HAL ADC 模块宏已启用，不引入不必要宏。`stm32f1xx_hal_rcc_ex.c` 若所用函数确需链接才增添；宏 `__HAL_RCC_ADC_CONFIG` 不需要该 .c。
7. 在 `main.c` 最小接入ADC init，保证现有四个模块初始化顺序不变。优先让 UartTask 在调度器启动后触发 ADC Start，防止初始化中断调用 FromISR。非必要不改 `interrupts.c`：DMA1_Ch1 IRQ 可由adc_app.c提供唯一强符号。
8. 只做一次本机可用 Keil 正常构建；给用户执行T2、T3。记录真实结果（没有实测则PENDING）；完成后生成简明 `doc/P4b/P4b_HANDOFF.md`。P4a与P4b都通过再形成 `doc/P04_HANDOFF.md` 供总控审核，不自行进入P5。

## 允许改动范围

- 新增 `App/adc/adc_app.[ch]`。
- 增量修改 `App/uart/uart_app.c`（以及确有必要时其头文件）、`Core/Src/main.c`、`Projects/RTnode.uvprojx`；可选 `Core/Src/interrupts.c` 仅增加DMA1 Ch1 IRQ且确保不重定义。
- 禁止：修改 Actuator、Control、Imu、Can 实现，TIM3/PWM、CAN线上协议、FreeRTOS tick/priority config、无关硬件驱动，伪造 ADC 绝对精度或测试结果。

## 失败时最短故障定位

串口可用但 `n` 不递增：核对 ADC EXTSEL/EXTTRIG、Continuous=DISABLE、DMA Circular/halfword/Ch1、TIM2计数时钟、`CCR2`在0与ARR之间、PWM2模式与 **CC2E**，以及启动顺序；**不可直接改用TIM2_TRGO**。仅在确有功能故障时定位，不开展大规模注入。
