# P4a 设计

## 1. 外设/调度

- USART1 PA9(TX, AF push-pull)、PA10(RX, input)，默认 AFIO remap 不改变，115200 baud, 8 data, no parity, 1 stop, 无硬件流控。
- DMA1 Channel5 -> USART1 RX，PERIPH_TO_MEMORY，`DMA_CIRCULAR`，byte/byte，memory increment，peripheral increment disabled；RX 静态 `uint8_t[256]`。
- UartTask 静态 TCB+栈，建议优先级 `tskIDLE_PRIORITY`、栈初值 **384 words**（最终结合 ARMCC 和现有栈余量检查），最长 **5ms** 主动消费检查；HT/TC/IDLE 可唤醒，但不应依赖 IDLE 才消费。
- USART1_IRQn 和 DMA1_Channel5_IRQn 使用 NVIC preempt prio6、subpriority0。调用 `FromISR` 仅在 scheduler 启动后允许，故 IRQ 启用 / DMA 开始应由 UartTask 启动阶段负责，或做等效安全时序设计。
- TX 初期仅 UartTask 调用 `HAL_UART_Transmit`，短固定报文及有限超时，不能在任何 ControlTask/ISR 中阻塞 TX；不引入 TX DMA。

## 2. 接收数据路径与绝对计数

USART1 DR → DMA 环形 256B → `producer_total`（累计写入字节数）→ `consumer_total`（累计已消费字节数）→ 行解析器。`write_pos = (256 - NDTR) % 256`；软件读取位置为 `consumer_total % 256`。

**仅两个取模指针不充分**：一整圈覆盖时两者可以相等，必须具备回绕计数或等效的生产者绝对进度。推荐在 DMA TC 中断维护整圈计数，并通过 NDTR 算出圈内位置；获取计数/NDTR 时协调 pending TC 位与 IRQ 临界区，避免“DMA 已回绕而 TC callback 尚未执行”的假零增量。HT 用于及时唤醒/交叉检查，不代替 TC 的回绕推断。可以选更简单但同等可论证的方法；必须在代码注释中说明临界区与计数竞态的处理方式。

在任务上下文计算 `unread = producer_total - consumer_total`：

- `unread == 0`：无数据。
- `0 < unread <= 256`：按绝对顺序处理每字节；可跨数组尾并继续至数组头。
- `unread > 256`：至少一个字节被覆盖，增加 OVF 计数，丢弃已损坏的待解析行，仅保留可信的最近缓冲段，并等下一个 `\n` 重同步。不能把损坏后的行尾当完整命令执行。

5ms 轮询是附加进度保证而非数学上的无限吞吐保证。115200 8N1 理论最大约 11520 bytes/s，整圈约 22.2ms。连续流/串口输出竞争下仍有可能覆盖，检测后必须明确丢弃/计数，不能静默把旧数据当新命令。没有任何要求无限持续突发零丢包。

## 3. IDLE 与 UART 错误

- UART USART1 IDLE IRQ：检测 IDLE 后按仓库 HAL 宏执行 **读 SR 再读 DR** 的清标志序列，再做最小通知；注意不能随意在 DMA 运行且 RXNE 待处理时额外读 DR 导致吞字节。根据源代码处理顺序管理 IDLE 与 error flag，不在 ISR 中解析命令。
- DMA HT/TC 回调：只记录可安全访问的轻量状态/任务通知；DMA TC 不应重调用 `HAL_UART_Receive_DMA`，Circular DMA 自行持续运行。
- **仓库版 `HAL_UART_IRQHandler` 在 DMA Rx 发生 FE/NE/ORE 等错误时会中止 DMA 接收**。须在 USART1 IRQ 正确交给 HAL 错误分支，并在 `HAL_UART_ErrorCallback` / DMA abort 完成通知后，由 UartTask 检查 HAL/DMA Ready 状态、清理损坏的半行、重新启动一次 circular DMA。不要在错误 ISR 内直接重启、不要无节制复位。异常无法恢复时只标记 UART 故障，**不改变现有执行器所有权或 CAN STOP**。

## 4. 行协议

- 仅支持 ASCII 行（`\n` 终结，忽略可选 `\r`）；区分大小写。
- 命令最多 32 bytes（**包括输入行缓冲空间，建议实际有效字符上限 31**），满时丢弃至下一次换行并产生一次 `ERR`；空行忽略。
- `PING\n` → `PONG\r\n`。
- `COUNT?\n` → `RX=<累计接收字节>,OVF=<覆盖或丢行次数>\r\n`；RX 定义应与 ISR 字节统计/消费统计保持一致，并在 interfaces.md 注明。
- 未定义命令 → `ERR\r\n`。P4a 可以对 `ADC?` 返回 `ADC NOT READY`，供 P4b 扩展。
- TX 只从 UartTask 触发：将每轮处理与回复数量保持有界，先消费 DMA 防止输出阻塞长期饿死 RX。分段发送时须保证不破坏已有 20ms ControlTask 周期。

## 5. 明确不做

不创建串口命令队列、通用帧协议、可靠 ACK、TX DMA、复杂日志、故障注入/软定时器、任何控制命令。
