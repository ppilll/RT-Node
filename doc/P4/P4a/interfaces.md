# P4a 接口契约

拟新增 `App/uart/uart_app.h`、`App/uart/uart_app.c`。下列是**对 Codex 的 API 契约**，不是现有已实现代码。

```c
#include <stdbool.h>

/* 在 main() 的既有初始化完成后、scheduler 启动前调用；创建静态任务及初始化所需硬件。 */
bool UartApp_Init(void);

/* ISR 所有权：优先在 uart_app.c 中定义下列唯一强符号；也可放 interrupts.c，但不得重复定义。 */
void USART1_IRQHandler(void);
void DMA1_Channel5_IRQHandler(void);
```

UartTask 只作为模块私有实现，不对其他任务开放串口发送 API。P4b 后唯一串口输出者仍是 UartTask。

## 单写者与共享状态

- DMA 硬件单写者：RX 环形缓冲区。ISR 只修改 HT/TC 计数、错误标志等少量原子状态。Task 是消费者、协议状态和 TX 的唯一软件写者。
- 必须使用 volatile/中断保护处理 ISR 可修改状态及一致计数快照；使用 FreeRTOS `FromISR` 时需符合集成优先级，调度器开始前不调用。
- 绝对计数与覆盖/丢行统计使用 `uint32_t`；模值与溢出语义在源码注释中固定，短时间项目运行允许 32-bit 回绕的自然模算但不可破坏 unread 判定。
- `RX` 建议定义为 DMA 累计已接收字节（producer_total），`OVF` 为已检测覆盖或过长行引起的丢行次数；回复不承诺是零误差串口物理计数（硬件错误导致无法进入 DMA 的字节不计）。

## HAL 强约束

- 正确配置、初始化 DMA handle 并 `__HAL_LINKDMA(&huart, hdmarx, &s_dma_rx)`。
- USART1 使用 `HAL_UART_Init`、`HAL_UART_Receive_DMA`；DMA IRQ 内调用 `HAL_DMA_IRQHandler(&s_dma_rx)`，USART1 错误 IRQ 走 `HAL_UART_IRQHandler`，IDLE 需自定义处理。
- `HAL_UART_RxHalfCpltCallback()`、`HAL_UART_RxCpltCallback()`、`HAL_UART_ErrorCallback()` 来自本仓库 HAL 的弱函数，可由 App 模块实现强版本。
- USART ISR 优先级6，DMA Channel5 优先级6。`FreeRTOSConfig.h` 最大 syscall 优先级 5，且 `configUSE_TASK_NOTIFICATIONS=1`。
- 不启用 `HAL_UARTEx_ReceiveToIdle_DMA`，本仓库无此 API。

## P4b 接口预留

不为 P4b 增加无用通用抽象。P4b 可在 `uart_app.c` 中添加对 `adc_app.h` 的引用及 `ADC?` 解析与每 200ms 自发报告。不要在 P4a 添加 ADC 假数据。
