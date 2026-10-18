# P4b 接口契约

新增 `App/adc/adc_app.h`、`App/adc/adc_app.c`，**下方是拟实施接口而不是现有代码**：

```c
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t raw;          /* 最近完整半窗的末尾样本，0..4095 */
    uint16_t avg;          /* 最近32个样本平均，0..4095 */
    uint16_t mv;           /* 使用假设VDDA=3300mV换算 */
    uint32_t n;            /* 已安全汇总的样本数；按32递增 */
    uint32_t missed;       /* 丢弃的过期半窗数 */
    bool valid;
} AdcApp_Result;

/* 下列接口由P4b确定最简单的初始化/启动分离时机。 */
bool AdcApp_Init(void);                 /* 设置资源、ADC与DMA，执行校准 */
bool AdcApp_Start(void);                /* RX系统可用后启动ADC DMA并使能TIM2 */
void AdcApp_Process(void);              /* 仅UartTask调用，处理已完整且未被覆盖的窗口 */
bool AdcApp_GetLatest(AdcApp_Result *out); /* 任务上下文，获取缓存快照 */

/* DMA1 Channel1 IRQ 的唯一强符号 */
void DMA1_Channel1_IRQHandler(void);
```

## 接口所有权

- `AdcApp_Init()` 仅一次，`AdcApp_Start()` 仅一次；P4b 有权根据工程HAL选择更短的单初始化方案，但必须保证“校准→DMA准备→TIM2首次事件”顺序、任务/IRQ状态正确。
- ADC模块拥有TIM2寄存器、ADC1、DMA1 Ch1、PA1配置、静态64×uint16数组和轻量窗口计数。UartTask拥有全部输出格式和USART1发送。
- `AdcApp_Process` **不得在 ISR 内调用**；应处理 HT/TC 待办窗口。`AdcApp_GetLatest` 返回的是已完成的平均值快照，不是指向 DMA 正在写的数据指针。
- P4b 可以增量修改 `App/uart/uart_app.c`：添加 `ADC?` 与每200ms一次报告；不得增加 UART TX 第二所有者、改变 P4a 协议核心或重构 UART RX。
- DMA Channel1 `HAL_DMA_IRQHandler`；HAL ADC 半完成/完成回调仅登记窗口。外设IRQ优先级6，`FreeRTOSConfig.h` 不变。

## 资源表

| 单元 | P4a | P4b | 冲突 |
|---|---|---|---|
| USART1 PA9/PA10 | RX Ch5 + TX | 复用输出 | 无 |
| DMA1 Ch5 | UART RX | 保留 | 无 |
| ADC1 PA1 | 不使用 | ADC1_IN1 | 与PA7无关 |
| DMA1 Ch1 | 不使用 | ADC RX | 无 |
| TIM2 CH2 | 不使用 | 内部波形触发 | 不修改TIM3 |
| SPI1 PA5/6/7 | IMU持有 | 不修改 | 无 |
| TIM3 PC6/7 | Actuator持有 | 不修改 | 无 |
| CAN1 PA11/12 | CAN持有 | 不修改 | 无 |
