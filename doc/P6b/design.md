# P6b 设计细则（IWDG + 任务进展）

## 1. IWDG 参数与计时误差

| 项目 | 候选值 |
|---|---|
| IWDG | STM32F103 内置独立看门狗；优先软件启动 Option Bytes |
| HAL | 已有 `HAL_IWDG_Init`, `HAL_IWDG_Refresh` |
| Prescaler | `IWDG_PRESCALER_64` |
| Reload | `1249` |
| LSI | 典型 40kHz，规格 30..60kHz |
| 超时名义 | `(1249+1)*64 / 40000 = 2.00 s` |
| 规格推导区间 | 60kHz约1.333s，30kHz约2.667s |
| HealthTask | 初版每 100 ticks（100ms） |
| ControlTask 进展门限 | 候选连续约 300ms 没有新的 wake_count 后停止刷新 |
| HealthTask 优先级 | 初版 prio0（ControlTask prio1 不变） |
| HealthTask 栈 | 初始候选 192或256 words，编译和实机按 stack high-water 核对 |

**不能**把 2s 当成实测定值。LSI 的频差需要体现在最终文档中。单颗器件 LSI 可能接近规格边界，T2 以实际复位结果确认，不凭串口停止判定。

## 2. IWDG 启动时机

P6a 的 `main()` 先 P1a 安全输出、读取复位标志、处理必要的 Flash 状态，正常 HAL/FreeRTOS 初始化。IWDG **不在 main() 开始、也不在 SysTick 中自动初始化**，而由 HealthTask 在 scheduler started 且至少两次观察到 ControlTask 进展后首次调用 `HAL_IWDG_Init()`。

理由：仓库 HAL `HAL_IWDG_Init` **先写 IWDG START KEY** 然后配置 PR/RLR，并利用 `HAL_GetTick()` 等待更新；此函数即使最后返回 HAL_TIMEOUT，硬件可能已开始倒计时，不能简单视为 OFF。HealthTask 执行时 SysTick 有效，且当前共享 tick 设计完整运行。

启动前的系统初始化故障（HAL 时钟、静态任务创建、BKP/Flash 状态处理）不被“软件尚未启用的 IWDG”保护；仍依靠既有 `P1a_FatalError` 立即安全关闭。配置应假设 Option Bytes = 软件启动；**如果实机 Option Bytes 被配置为硬件自动启动，先阻止 P6b 放行**，需专门评估上电窗口、Flash 操作和初始化耗时。

## 3. 单一刷新位置与状态机

HealthTask 按 `xTaskDelayUntil` 做静态周期调度：

```
首次启动：读取 Control_GetDiagnostics().wake_count -> 建立基线
后续循环：只要 wake_count 持续变化且未超过 300 ms 无进展，认为进度可用
连续至少两个有效监督间隔后：调用 HAL_IWDG_Init（只调用一次）
IWDG 正常运行：满足进展判断才调用 HAL_IWDG_Refresh
wake_count 停滞超过门限 / 关键数据读取异常：停止刷新，等待 IWDG 复位
调度器完全停止：HealthTask 不运行，无法刷新 -> IWDG 自然复位
```

Count32 按无符号溢出处理（`new != old` 即进展，不要求单调差值为正），启动初期 0 与前两次读数要明确区分。进度判断只证明 ControlTask 到达循环入口，不能作为端到端运动输出正确性的证明。HealthTask 还需要自身被调度，这提供有限的调度活性证据；若 HealthTask 低优先级被持续饥饿，也会误复位，作为 T3 验证项。

**重要**：当 ControlTask 因 `CONTROL_FAULT_DEADLINE`、`CONTROL_FAULT_DRIVER` 等已正确完成 `Actuator_ForceSafeOutput`、进入故障终态，但 `wake_count` 仍继续更新时，**正常刷新 IWDG**。这样有利于保留串口诊断；不得因为 `mode==FAULT` 单独停喂。

**也不得因为以下事件停喂**：IMU FAILED 但当前 REMOTE 或 DISABLED；普通 CAN heartbeat 丢失；REMOTE TTL 超期后已安全 Disarm；UART 接收故障；OLED/BME280/ADC 局部故障。

## 4. 异常、任务停滞与复位循环

- `P1a_FatalError()`、`AppFatal_Assert()`、`vApplicationStackOverflowHook()` 均已直接关闭并锁存执行器，先保留该行为。IWDG 启动后，非返回循环不刷新，最终重启；重启后 P6a 立即写入 LOCK，不能自动恢复旧控制。
- 如果曾经触发 IWDG 后 Flash 日志尾部已经是 LOCK，那么再次 IWDG 无需追加，避免每次复位消耗存储位；HealthTask 正常运行但维持锁定仍喂狗。
- 若 IWDG 重启后原故障再次阻塞 ControlTask，可能连续复位，但每轮保持已锁定并尽可能提供可读的 `HEALTH?`；不实现 Flash 复位次数循环计数、回滚或复杂 recovery state machine。
- IWDG 启动前的 fatal 可能无限停留在原 fatal loop；这是软件启动的明确覆盖边界，不宣称全生命周期看门狗覆盖。

## 5. 轻量输出

- 首选扩展 P6a USART1 `HEALTH?`，字段包括 `CTRL`（进展正常/停滞/初始化）、`WDG`（`ON/OFF/STARTERR`）、`RESET`（BootSnapshot）、`LOCK`、`POLICY`（如空间允许）。
- 遵守既有 UartTask 唯一 TX、reply[64]、30B 发送 chunk 及已有 ADC 轮询要求；最终 ASCII 长度按最坏字符串核对，必要时缩为 `H C=RUN W=ON R=IWDG L=1 P=LOCK`。
- 如 OLED 内容允许，可在原页面加入极简文字；不强制增加第五页。绝不修改 CAN STATUS 数据布局。

## 6. T2 的受控停止刷新（只属于测试固件）

- 单独的编译宏，例如 `P6_WDG_TEST_ENABLE` 默认 0；再配合 `P3B_OUTPUT_STAGE == 0` 等编译时硬约束，以及显式无负载确认宏；宏启用时最多模拟一次停止刷新。
- 注入条件必须是 HealthTask 已成功启动 IWDG 后，且 P6a BootSnapshot 的当前 reset_reason != IWDG；不要在控制正常授权下停喂，更不通过 UART 或 CAN 指令触发。
- 生产镜像中测试宏、调用路径、权限开关全部禁用，不允许留下普通操作可触发的停喂命令。
- 只凭 `HEALTH RESET=IWDG` 加重新启动现象才证明 T2 发生实际 IWDG 复位；测试固件应在 IWDG 重启之后退出注入，允许持续读状态。

## 2026-10-18 实施补充与理由

1. main 在安全输出、BootCapture之后，HAL/Flash之前只读检查 `FLASH->OBR`：IWDG_SW须为1且OPTERR为0，否则进入原P1a_FatalError。没有改写Option Bytes，没有早期无条件喂狗。硬件自动启动配置因此不能放行；该分支未启动UART，失去串口诊断，需离线检查启动配置。这是安全优先的可观测性代价；本轮未读取实机Option Bytes。
2. HealthTask静态栈256words、prio0、100ticks；ControlTask原wake_count不增加其它心跳。超过300ticks的无进展/观察间隔锁存停喂，不重试；已安全FAULT而wake_count继续变化时正常喂狗，EMPTY/LOCK/BAD亦不单独停喂。
3. `HAL_IWDG_Init`成功时本身会内部reload一次，保持原HAL，不把它误计为第二处应用Refresh。应用Init和Refresh均只在HealthTask，各一处；Init之前发布START、超时发布STARTERR，不能把已启动但配置失败说成OFF。
4. T2用 `P6_WDG_TEST_ENABLE=1` + `P6_WDG_TEST_UNLOADED_ACK=1` + `P3B_OUTPUT_STAGE=0`，另禁止历史输出测试。默认两测试宏均0，生产不能保留ACK。
5. T2在IWDG成功启动至少5000ticks后，仅当控制Disabled、执行器initialized/disabled/无driver fault，且启动策略是EMPTY/AUTO、非IWDG复位时锁存停喂。**LOCK/MANUAL/BAD下不注入**，因此IWDG后的NRST/POR也不会再次注入。只看reset_reason!=IWDG不足以保证这一点，故增加持久策略判定；没有破坏MANUAL策略来重复测试的旁路。
6. 所有串口示例及实测识别改用interfaces.md的短字段映射；真实T2须同时观察新启动及R=IWDG，不能用串口失联/T=1代替证明。旧P6a日志与handoff依用户要求不更新。
