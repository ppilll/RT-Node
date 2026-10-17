# P3 CAN 协议与跨模块接口（V1.0，冻结）

此文档是 **P3a 与未来 P3b 共用的线路协议规范**。用户已批准 ID、字段、时限和停止/切换原则；本页将必要的未定义边界收口。P3a 必须编解码同一格式，但 **P3b 的控制效果在 P3a 不得启用**。

## 1. 通用规则

- Classical CAN，标准 11-bit CAN ID（IDE=0），数据帧（RTR=0），**所有类型 DLC 必须等于 8**；不接受扩展帧、远程帧、DLC 不符或未知 ID。
- 多字节值一律 Little Endian；整数无符号，PWM 单位 µs；所有 `reserved` 必须为 0；协议版本固定 `0x01`。
- CAN 链路层自带 CRC，本版不增加应用层 CRC；只针对可信的封闭 CAN 网络，不保证抗伪造/恶意重放。
- 所有接收时间使用 STM32 本地 1kHz 32bit RTOS Tick，在 RX ISR 入队时记录；禁用跨设备时钟同步假设。
- 数据消息最新合法 `seq` 按 16-bit 半环新旧比较：`delta=(uint16_t)(seq-last_seq)`，`0<delta<0x8000` 判为新；第一帧允许任意 seq。相同/倒退/跨度≥半环都不得刷新接收时刻。
- `epoch` 是 STM32 管理的 16-bit 会话代号；开机默认0，在实际发生模式切换或从非 DISABLED 接受 GLOBAL_STOP 时加1（模65536）；模式切换后旧 epoch 命令必失效。跨 MCU 重启的防重放**不在 V1.0 保证范围**；RK3588 必须先读取新 STATUS 再发控制。

| ID | 方向 | 消息 | DLC | 发送时机 |
|---|---|---|---:|---|
| `0x180` | RK→STM | REMOTE_CONTROL | 8 | REMOTE 活跃时 20ms |
| `0x181` | RK→STM | MODE_REQUEST | 8 | 事件触发 |
| `0x182` | RK→STM | REMOTE_STOP | 8 | 事件触发，允许重复 |
| `0x183` | RK→STM | HEARTBEAT | 8 | 100ms |
| `0x280` | STM→RK | STATUS | 8 | 100ms + 处理模式/STOP/状态变化 |

## 2. REMOTE_CONTROL `0x180`

| Byte | 字段 | 范围与约束 |
|---|---|---|
| B0 | `epoch_lo` | uint16 LSB |
| B1 | `epoch_hi` | uint16 MSB |
| B2 | `seq_lo` | uint16 LSB |
| B3 | `seq_hi` | uint16 MSB |
| B4 | `pan_lo` | uint16 LSB，µs |
| B5 | `pan_hi` | uint16 MSB，µs |
| B6 | `tilt_lo` | uint16 LSB，µs |
| B7 | `tilt_hi` | uint16 MSB，µs |

P3b 仅在 WAIT_REMOTE / PRIMING_REMOTE / ACTIVE_REMOTE（见状态编码）、epoch 一致、seq 新、Pan∈[1000,2000]、Tilt∈[1200,1600] 时接受。第一次 REMOTE_CONTROL 只能在显式 MODE_REQUEST(REMOTE) 产生新 epoch 后起效；旧帧不缓存跨会话使用。内部 `ControlCommandSnapshot` source=`CONTROL_SOURCE_REMOTE_CAN=3`、32bit generation（由每次接受的新 CAN seq 自增）、issued_tick=ISR接收tick、ttl_ticks=100、valid=true、两轴脉宽。**同一 seq 再读一次不重复调用 `ControlSession_Accept()`**。

P3a：解析合法性后记录/丢弃，**永不**使能 REMOTE 或创建控制命令快照；此帧不要求逐帧 ACK。

示例：`180#01000100DC05DC05` = epoch=1、seq=1、Pan/Tilt=1500µs。

## 3. MODE_REQUEST `0x181`

| Byte | 字段 | 范围与约束 |
|---|---|---|
| B0 | `version` | 固定1 |
| B1 | `requested_mode` | 0=DISABLED, 1=LOCAL, 2=REMOTE |
| B2 | `action_id_lo` | uint16 |
| B3 | `action_id_hi` | uint16 |
| B4 | `expected_epoch_lo` | uint16 |
| B5 | `expected_epoch_hi` | uint16 |
| B6 | `reserved` | 0 |
| B7 | `reserved` | 0 |

`action_id` 由 RK3588 单调推进，MODE_REQUEST 与 REMOTE_STOP 共用计数域。对 **MODE_REQUEST** 执行 16-bit 半环新旧判断；重复相同 ID/相同内容不得重新切换，重复相同 ID/不同内容拒绝；旧 ID 拒绝。新的、语法合法但 epoch/状态不符合的请求仍消耗 `action_id` 防回退门槛，下一次修正必须用更新 ID。`expected_epoch` 必须与当时控制状态一致。目标等于当前模式时只确认，不更新 epoch、不重建会话。实际切换时由 ControlTask 先 Disable、废弃旧命令、更新 epoch，再进入目标 WAIT 状态。

P3a：所有模式请求（包含 DISABLED）均 **业务拒绝**，不得切换模式；合法格式请求产生 `STATUS`，`reason=REQUEST_REJECTED`，bit5=0，`last_action_id` 为本次请求 ID；不修改 epoch。

示例：`181#0102010000000000` = 请求 REMOTE、action_id=1、expected_epoch=0（仅 P3b 可接受）。

## 4. REMOTE_STOP `0x182`

| Byte | 字段 | 范围与约束 |
|---|---|---|
| B0 | `version` | 1 |
| B1 | `scope` | `0x01` = GLOBAL_STOP |
| B2 | `action_id_lo` | uint16 |
| B3 | `action_id_hi` | uint16 |
| B4 | `reserved` | 0 |
| B5 | `reserved` | 0 |
| B6 | `reserved` | 0 |
| B7 | `reserved` | 0 |

格式合法的 STOP **不需要 epoch 匹配，也不得因 action_id 过旧被拒绝**；在 P3b 优先级高于正常模式/命令，对本地/远程都有效。重复 STOP 幂等；FAULT 不可由 STOP 清除。在非 DISABLED 模式接受 STOP 会先 Disable，作废待处理模式/远程命令，再更新 epoch 并进入 DISABLED；已经 DISABLED 时接受 STOP 不重复改变 epoch。安全起见，当 STOP 的 action_id 比此前新时可推进后续 MODE_REQUEST 的动作门槛，旧 STOP 不使门槛倒退。这里的 `action_id` 仅用于反馈关联，不作为停止许可。

P3a：STOP 为已禁用系统中的安全空操作，格式合法则反馈接受（STATUS bit5=1、reason=REMOTE_STOP），无 Actuator 调用/模式变更。示例：`182#0101020000000000`。

## 5. HEARTBEAT `0x183`

| Byte | 字段 | 范围与约束 |
|---|---|---|
| B0 | `version` | 1 |
| B1 | `flags` | 0 |
| B2 | `counter_lo` | uint16 |
| B3 | `counter_hi` | uint16 |
| B4 | `reserved` | 0 |
| B5 | `reserved` | 0 |
| B6 | `reserved` | 0 |
| B7 | `reserved` | 0 |

期望 100ms/帧；首次合法 counter 任意，之后仅 **新 counter** 刷新 heartbeat tick；重复/过旧不刷新。`age<500ms` 置 `STATUS.flags bit2=1`。心跳 **永不**刷新 REMOTE_CONTROL 的 ttl，LOCAL_FOLLOW 不因心跳失效停机。示例：`183#0100010000000000`。

## 6. STATUS `0x280`

| Byte | 字段 | 取值 |
|---|---|---|
| B0 | `version` | 1 |
| B1 | `state` | 下表 |
| B2 | `reason` | 下表 |
| B3 | `flags` | 位表 |
| B4 | `epoch_lo` | uint16 |
| B5 | `epoch_hi` | uint16 |
| B6 | `last_action_id_lo` | 最后一次经格式校验而被处理的 MODE_REQUEST/STOP 的 action_id |
| B7 | `last_action_id_hi` | 同上 |

`state`：0 DISABLED；1 WAIT_LOCAL；2 PRIMING_LOCAL；3 ACTIVE_LOCAL；4 WAIT_REMOTE；5 PRIMING_REMOTE；6 ACTIVE_REMOTE；7 FAULT。

`reason`：0x00 NONE；0x01 KEY1_STOP；0x02 REMOTE_STOP；0x03 REMOTE_TIMEOUT；0x04 CAN_RX_OVERFLOW；0x05 REQUEST_REJECTED；0x80+n 对应 `ControlFault` 枚举 n（不能伪造故障）；未列出的代码禁止随意分配。

`flags`：bit0 actuator_enabled；bit1 IMU_READY 且数据新鲜；bit2 最近有效 heartbeat <500ms；bit3 最近有效 REMOTE_CONTROL <100ms 且当前 epoch/模式匹配；bit4 CAN Bus-Off；bit5 最近处理的 MODE_REQUEST/STOP 已接受；bit6..7=0。初始没有动作时 `last_action_id=0` 且 bit5=0，不代表 action 0 已处理。

处理 MODE_REQUEST/STOP 时优先安排一次状态帧；其 `last_action_id` 与 bit5 表示这次处理结果，拒绝时 `reason=REQUEST_REJECTED`。正常周期 STATUS 是最新状态快照，**不是永久保存的独立 ACK 历史**。多动作突发时发送可能合并，RK3588 应串行等待确认或读取更新的状态/epoch，再发下一个模式请求。`reason` 体现最近停止/拒绝/故障事件；无新事件时可维持上次 reason，成功模式选择清除普通拒绝 reason，故障 reason 不得被新消息清除。STM32 不能在 Bus-Off 下保证 STATUS 及时发送。

P3a 正常运行时 `state=DISABLED`、`epoch=0`、`actuator_enabled=0`、`remote_command_fresh=0`；若现有 ControlTask 或 Actuator 实际已进入致命 FAULT，则必须报告 `state=FAULT` 和已有故障原因，不能以 DISABLED 隐瞒故障。IMU_READY/heartbeat/busoff 标志反映真实状态；STATUS 定时与事件触发并存。

## 7. 时间/停止/授权冻结（P3b 使用，P3a 禁止执行）

- `ControlTask` 20ms；REMOTE_CONTROL RK3588 建议20ms；远程 TTL **100ms**，age≥100ms 禁止继续应用旧目标，禁用输出；心跳 100ms，链路判定500ms。
- REMOTE 目标变化限制 **每 Control 周期至多±2µs/轴**，以本次中位启动输出为初值，最终经 Actuator 限幅。此限速在远程路径实施一次，P3a 不需要。
- 正常 Disable/REMOTE_STOP/REMOTE_TIMEOUT → 可再次授权，但需要新的 MODE_REQUEST+epoch+控制目标；LOCAL 必须新 KEY0 边沿。致命故障继续保留原 Actuator latch/ControlSession terminal 原则，不能自动恢复。
- 运行中切换必须经 Disable 边界：任何既往 remote/local 快照不允许重放。CanTask 不得直接调用 Actuator；ControlTask 为唯一正常写者。
- 模式请求和 STOP 的实质决策由 ControlTask 作出（P3b），CanTask 只完成解包、跨任务投递与 STATUS 发布；P3a 没有模式仲裁，仅固定拒绝模式请求。
- 接收队列丢失帧导致控制事件不可信时，P3b 在 ControlTask 进入 Disable 并需新授权；P3a 只报告 CAN_RX_OVERFLOW，不触碰执行器。

## 8. 初版内部边界（建议稳定名称）

- 新增 `App/can/can_app.h/.c`、`App/can/can_protocol.h/.c`（若纯解包函数足够短，可合并到 can_app.c），只在此模块内维护 HAL CAN 实例与 FIFO0 RX；公开 `bool CanApp_Init(void)`（main 在启动调度器前调用，创建静态任务/队列并做无中断接收的初始化）。
- CanTask 在 scheduler 启动后调用 HAL_CAN_Start()、ActivateNotification()，保证 FreeRTOS FromISR API 不会在任务调度器未运行时触发。
- CAN IRQ vector `USB_LP_CAN1_RX0_IRQHandler`/`CAN1_SCE_IRQHandler` 可以在 CAN 模块中提供强定义；尽量不修改原 `interrupts.c`。不改已有 SysTick/SVC/PendSV/fault handlers。
- HAL 事件 ISR 从 FIFO0 取到的帧带 ISR 采样 tick 非阻塞入深度8的静态 FreeRTOS Queue；CanTask 通过阻塞队列读取，100ms周期状态调度；不需要再叠加通知/第二套队列。
- 未来 P3b：CanTask 单写完整 remote mailbox、ControlTask 单读；ControlTask 单写状态/epoch mailbox、CanTask 单读。短临界区完整复制即可，不能按字段交错访问。STOP 使用独立待处理标志，不能被后来普通控制命令覆盖。
- 不在此阶段新增 `ControlSession` 再授权 API；P3b 另立设计/实施任务，遵守本页冻结的行为。具体内部结构可随源码最小化调整，但不改变协议布局、所有权、安全规则。
