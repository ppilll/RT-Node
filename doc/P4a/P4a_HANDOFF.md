# P4a 交接 — PENDING（2026-10-18）

代码已实现，Keil 构建通过；用户 T1 尚未提供，**P4a 不标记 PASS，不建议进入 P4b**。未开发 P4b。

## 文件与资源

- 新增 `App/uart/uart_app.h/.c`：私有静态 UartTask，优先级 0，384 words（1536B）栈；256B RX 环形缓冲、32B 行缓冲、UART/DMA handle、静态 TCB。无动态分配、命令队列、TX DMA 或额外日志框架。
- `Core/Src/main.c`：只在原有 `Actuator_InitSafe → Control_Init → Imu_Init → CanApp_Init` 后追加 `UartApp_Init`，失败走已有 `P1a_FatalError`；SysTick 交接保持不变。
- `Projects/RTnode.uvprojx`：增加 UART include 路径、`uart_app.c` 和仓库自带 `stm32f1xx_hal_uart.c`。
- `interrupts.c`、FreeRTOSConfig、HAL 源码、CAN/IMU/Control/执行器/TIM3 未改。两个新 IRQ 强定义均在 `uart_app.c`。
- Keil 正常构建更新 `Projects/` 下既有 AXF/HEX、map、调用图和依赖等常规产物；没有新增 evidence 树。

## DMA、协议与故障行为

- USART1 PA9/PA10，115200/8N1、无流控；DMA1 Channel5，byte/byte、memory increment、circular。仅使能 DMA1 时钟，不整体复位 DMA1，不改 AFIO remap。
- USART1/DMA1_Channel5 IRQ 均为抢占优先级 6、子优先级 0；NVIC 和 RX DMA 只在 UartTask 启动后开启。IDLE、HT、TC 通知任务，任务空闲时最多等待 5 ticks 后主动检查 NDTR，连续流不依赖 IDLE。
- TC 累计圈数；临界区协调 pending TC 与 NDTR，处理已回绕但 TC callback 尚未执行的情况。32 位进度自然回绕，索引按 256 取模。读取字节后再次检查生产进度，避免 DMA 在读取期间覆盖产生错误命令。
- unread > 256 时 OVF 加一，保留最近半圈以留出消费余量，清除半行并丢弃至 LF，再输出一次 ERR。超长行也仅丢弃一次并在 LF 输出 ERR；缓冲有效字符最多 31，忽略 CR，空行忽略，命令区分大小写。非可打印 ASCII 丢弃该行并返回 ERR。
- `PING\n` → `PONG\r\n`；`COUNT?\n` → `RX=<uint32>,OVF=<uint32>\r\n`；其余命令 → `ERR\r\n`，没有 ADC 假数据。
- RX 是 DMA 累计生产进度的快照，包含命令自身的 LF/CR，也可能包含已经到达的后续输入；错误恢复前冻结 DMA 保存进度，重启后继续累计。未进入 DMA 的硬件丢失字节不计。OVF 是检测到的 DMA 覆盖事件、超长行、接收错误恢复导致丢行的次数，不是丢失字节数；普通未知命令不增加 OVF。RX/OVF 均按 uint32 自然回绕。
- 每轮最多处理 64 字节、一条回复；回复最长 30B，只有 UartTask 调用一次 `HAL_UART_Transmit`，共享超时参数 3ms（旧 HAL 超时按 tick 判定，并受任务抢占影响，不是墙钟硬上限）。发送后立即继续消费，TX 和解析均可被优先级 1 的 ControlTask 抢占。
- IDLE 遇 RXNE 待 DMA 搬运时不读 DR，屏蔽 IDLE 中断并由任务后续检查清除；正常清除使用仓库宏的 SR→DR 顺序。错误先交给 HAL，保留其 DMA abort 和完成回调。每个错误事件只在任务态检查 UART/DMA READY 并重启一次；失败或 TX 异常仅停用 UART 并置私有故障标志，不调用 CAN STOP 或执行器接口。

## 真实构建结果

- 工程：`Projects/RTnode.uvprojx`，Target 1。
- 使用 `D:/MKD5.36/UV4/UV4.exe -b ... -t "Target 1" -o .../Projects/P4a_build.log -j0`。先完成一次全量正常构建，再对错误路径复查后的最终代码增量编译、链接和生成 HEX。
- 实际编译器：**ARMCC 5.06 update 7 (build 960)**。`armcc --vsn` 和最终 `uart_app.o` 的 fromelf 编译器记录均确认此版本。uVision 文本日志首行显示注册名称 `V6.16`，但工具目录为 ARMCC/Bin，对象记录明确是 ArmCC 5.06u7，不能仅凭首行判断编译器。
- 最终结果：**0 Error(s), 0 Warning(s)**；Code=26506，RO-data=382，RW-data=184，ZI-data=9960。最终增量构建日志耗时 2 秒。
- 固件：`Projects/RTnode.hex`；构建摘要：`Projects/P4a_build.log`（被现有 `*.log` 忽略规则忽略，保留本机文件）。
- 链接调用图给出 UartTask 可解析调用链 Max Depth=124B，小于 1536B 配置；此静态值不代替运行时栈与抢占验证。源码保留私有栈余量低水位采样。未声称实机验证。

## 用户 T1 操作（唯一一组）

1. 用现有 BOOT0=1+RESET 流程下载本次 `Projects/RTnode.hex`。随后 **BOOT0=0+RESET** 运行应用，关闭下载软件的 COM 占用，串口助手设 115200/8N1、无流控。以下 `\n` / `\r` 必须发送实际 LF / CR 字节，不能输入反斜杠文字。
2. 发送 `PING\n`，应收 `PONG`；发送 `COUNT?\n`，应收 RX/OVF。若上电后只发这两条且均使用 LF，RX 应为 12、OVF 为 0。
3. 分两次发送 `PI`、`NG\r\n`，中间间隔例如 100ms；第一次不应回复，第二次换行后应仅回复一次 PONG。
4. 一次批量连续发送 100 条 `PING\n`（500B，不在人为设置的每条间隔中等待 IDLE），检查 100 次 PONG。待回显完成后再重复两批，累计超过多个 256B 周期，再发送 `COUNT?\n`。这是同一组 T1 的持续流和跨环形边界操作，负载应控制在串口收发可服务范围。若严格按上述顺序、没有其他输入，最后 RX 为 1525；实际输入行尾不同则按实际字节核对。
5. 按原 `test_plan.md` 四个操作分别记录“操作、预期、实际、PASS/FAIL”，附必要回显，说明现有任务有无可见异常；没有在线调试器也可完成。若普通负载没有覆盖，不必另外制造故障；若出现覆盖，需观察 ERR、后续 COUNT 的 OVF 增量及换行后恢复。

## 待确认事项与边界

| 项目 | 当前状态 |
| --- | --- |
| T1 基本命令与 COUNT | PENDING，待用户实测 |
| T1 分段输入 | PENDING，待用户实测 |
| T1 连续输入 | PENDING，待用户实测 |
| T1 跨多个环形周期、原任务无可见干扰 | PENDING，待用户实测 |
| P4a 整体验收 | PENDING，不能标记 PASS |
| 是否进入 P4b | 否，等待用户确认 T1 |

当前没有编译阻塞项。UART 错误恢复、实际栈余量、真实调度/接线行为尚未经硬件验证，不新增额外验收组。TC 是硬件粘滞标志，要求中断不能连续屏蔽超过一圈（115200 8N1 约 22.2ms）；本模块没有引入此类长临界区，也不承诺无限突发零丢包。
