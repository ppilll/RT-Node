# P4b ADC/TIM2/DMA 设计

## 1. 定时器与 ADC 的正确硬件触发

**硬性约束**：STM32F103 的 ADC1 regular group 支持 `ADC_EXTERNALTRIGCONV_T2_CC2`；不能把 `TIM2_TRGO` 当作此组合法触发。仓库旧版 HAL 的该宏在 `stm32f1xx_hal_adc_ex.h`。`ADC1.Init.ContinuousConvMode=DISABLE`，禁止通过软件循环 `SWSTART` 冒充硬件1kHz触发。

- APB1 timer clock 72MHz（APB1 prescaler2时定时器输入为2×PCLK1）。设 TIM2 PSC=71、ARR=999 → 1MHz计数器、每1000计数一次周期，即1kHz。
- TIM2_CH2 CCR2=500；建议 **PWM2 模式、active-high、CC2E=1**，在计数到CCR2时内部 OC2 输出产生一个上升沿，每周期触发一次 ADC。TIM2 只使用通道2内部触发：**GPIO PA1 始终保持模拟输入模式，不配置 TIM2_CH2 GPIO AF 输出，且不重映射 TIM2**。TIM2 不开每1ms的CPU中断，也不启动 TIM2 DMA。
- 该系列的 ADC 外部“CC”触发可能依赖内部通道输出沿而非仅 `CC2IF` 状态；不能只设置 Frozen/Timing compare 然后看到 CC2IF 就认定ADC已转换。PWM2 + CC2E + 适当 CCR2 边沿的配置是为了避免此陷阱。详见 <https://community.st.com/stm32-mcus-products-25/triggering-adc-1-with-timer-1-stm32f103-95975>（同系列对 CC 外部触发的讨论）。实现后必须用 DMA 累计样本数证实触发持续。
- TIM2 允许直接寄存器配置以避免为简单触发引入完整 HAL TIM 文件。直接写寄存器时仅操作 TIM2，不得复位 TIM3；不要整体覆盖 AFIO->MAPR。

## 2. ADC 配置与时钟

- `__HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6)`：PCLK2=72MHz -> ADC clock 12MHz（低于14MHz上限），不要改变 PCLK1/PCLK2 分频。
- ADC1 regular单通道，IN1/PA1、12-bit（F103硬件）、右对齐、Scan=DISABLE、Continuous=DISABLE、NbrOfConversion=1、Discontinuous=DISABLE、外部触发 `ADC_EXTERNALTRIGCONV_T2_CC2`。
- PA1 `GPIO_MODE_ANALOG`，ADC_SAMPLETIME 初始 `ADC_SAMPLETIME_71CYCLES_5`（预留较保守的采样保持时间）；可依据确认的输入源阻抗调整，不能为了提高频率删去必要采样时间。
- 用仓库版 `HAL_ADC_Init` + `HAL_ADC_ConfigChannel` + `HAL_ADCEx_Calibration_Start`（校准必须在转换/DMA开始前执行）；ADC 时钟设置和 ADC MSP 时序要正确。
- DMA1 Channel1，PERIPH_TO_MEMORY、Circular、memory/periph data alignment HALFWORD、memory increment、periph increment disabled。静态 **`uint16_t adc_dma[64]`**。仓库 HAL_ADC_Start_DMA 的 `pData` 类型是 `uint32_t*`，仅地址传递处显式转换；**不能据此把 DMA 宽度改成32-bit或把 buffer 误建成 uint32_t[64]**。

## 3. DMA窗口与输出

TIM2_CH2周期事件 → ADC DR → DMA1 Channel1 → 64个16-bit样本循环写入。

- DMA HT：前32样本完整；DMA TC：后32样本完整。ISR只记录已完成半窗序号或代数，不在ISR中32次求和或串口发送。
- UartTask每≤5ms检查完成窗口，安全取得已完成且尚未被DMA覆盖的半窗并求平均。HT→TC约32ms，读取时要保证 DMA 尚未回到待处理半窗；如果调度过晚则丢弃该窗并记录轻量 `missed` 计数，不输出混合代数的结果。若实现采用短临界区复制32样本，记住 DMA 不受 NVIC 关中断控制；真正的数据所有权依赖写半窗和时间窗口，不仅是临界区。
- 缓存最新结果：`raw` 最新窗口最后样本、`avg` 32样本整数平均、`mv` 基于 VDDA=3300mV 假设换算、`n` 已接受样本总数（每成功消费一窗 +32）。除法用 `avg*3300U/4095U`，32位范围安全。必要时 `missed` 一并输出。
- `ADC?` 返回最近快照，`ADC: raw=...,avg=...,mv=...,n=...`；自发输出周期 **200ms**（建议与UartTask现有主循环结合）。最初没有有效样本时返回 `ADC NOT READY`。不引入 ADC 独立任务、通用串口日志框架或第三个 UART TX 所有者。
- 若 ADC 硬件初始化/运行失败，记录简单 ADC 状态并保持 Control/CAN/IMU 逻辑不受影响；不得将 ADC 值接入 STOP 或模式仲裁。

## 4. 启动/竞争时序

合理顺序：UartTask已启动可输出诊断 → 配置ADC时钟/GPIO/DMA/ADC → ADC校准 → `HAL_ADC_Start_DMA`进入等待外部触发 → **最后**使能TIM2计数；避免启用定时器后才准备好DMA导致丢第一个触发或误判 DMA 停止。P4b 可选择 ADC 初始化硬件在main前完成而由UartTask调起采样，或在UART任务中完整初始化；须遵守 HAL tick 在调度器启动前后交接、初始化失败真实报告、不打断既有Init顺序。

DMA1 Channel1/Channel5 并行但不可整体复位DMA1；ADC DMA IRQ优先级6，若使用 FromISR，满足 FreeRTOS最大syscall优先级5。TIM3照旧。

## 5. 功能边界与测量限制

不把ADC当停机输入、不实现精密标定、不以逻辑分析仪验证模拟电压。不把约1kHz触发的寄存器设计计算直接写成经过实测的定时精度。只确认DMA持续采样、样本计数合理增长、读数随安全输入变化。
