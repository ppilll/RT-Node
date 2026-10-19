> P7更正：reason01线上数值不变，当前源码含义为KEY0(PE4) STOP锁存；KEY1(PE3)仅OLED翻页。本页其他旧按键授权描述属于历史设计，当前以[现场指南](../P7/operator_runbook.md)为准。

# P3b 冻结接口契约 V1.0

> **优先级声明**：`doc/P3a/interfaces.md` 是 P3a/P3b 共享的 CAN 线上协议权威文件。P3b 不更改 ID、DLC、字节序、取值、状态码、reason码或epoch/seq/action语义。本文仅固定 P3b 内部数据接口和控制效果；若出现矛盾先停止修改并在 handoff 标明，不可悄悄修改线协议。

## 1. 线上逐字节索引（全部 Classical CAN 标准数据帧，DLC=8，Little Endian）

| ID | 报文 | B0 | B1 | B2 | B3 | B4 | B5 | B6 | B7 |
|---|---|---|---|---|---|---|---|---|---|
| 0x180 | REMOTE_CONTROL | epoch_lo | epoch_hi | seq_lo | seq_hi | pan_us_lo | pan_us_hi | tilt_us_lo | tilt_us_hi |
| 0x181 | MODE_REQUEST | version=1 | requested_mode(0/1/2) | action_lo | action_hi | expected_epoch_lo | expected_epoch_hi | 0 | 0 |
| 0x182 | REMOTE_STOP | version=1 | scope=1 | action_lo | action_hi | 0 | 0 | 0 | 0 |
| 0x183 | HEARTBEAT | version=1 | flags=0 | counter_lo | counter_hi | 0 | 0 | 0 | 0 |
| 0x280 | STATUS | version=1 | state | reason | flags | epoch_lo | epoch_hi | last_action_lo | last_action_hi |

0x180 Pan合法 1000..2000μs，Tilt合法1200..1600μs；必须与当前 epoch 相同，seq首次任意、后续 `(uint16_t)(seq-prev)` 满足 `0<delta<0x8000` 才算新控制；本地 ISR tick 是唯一新鲜度源。0x181 除语法还需正确 epoch/动作序号，只有 ControlTask 可最终接受模式；0x182只要语法合法就必须立即成为待处理全局 STOP，不依赖 epoch、旧action_id也不得阻挡。0x183 heartbeat 仅更新 bit2，**不得刷新远程命令年龄**。

STATUS state：`0 DISABLED, 1 WAIT_LOCAL, 2 PRIMING_LOCAL, 3 ACTIVE_LOCAL, 4 WAIT_REMOTE, 5 PRIMING_REMOTE, 6 ACTIVE_REMOTE, 7 FAULT`。reason：`00 NONE, 01 KEY0_STOP, 02 REMOTE_STOP, 03 REMOTE_TIMEOUT, 04 CAN_RX_OVERFLOW, 05 REQUEST_REJECTED, 0x80+n ControlFault n`。flags：bit0真正 enabled、bit1 IMU ready/fresh、bit2 heartbeat <500ms、bit3 REMOTE snapshot fresh (<100ms)/epoch匹配且模式适用、bit4 bus-off、bit5最近动作接受；6..7为0。STATUS 不是保证送达的专用ACK，模式动作应串行等待反馈。

## 2. P3a源码中真实接口及改变范围

已存在：`App/can/can_app.h` 仅公开 `bool CanApp_Init(void)`，`can_app.c` 内含 IRQ/FIFO/Queue/CanTask、`ProcessFrame`、`MakeStatus`、`SendStatus` 等静态函数；`CanRxFrame` 存在 `received_tick`。P3a 状态变量目前仅为模块私有，**没有跨 ControlTask 的 remote mailbox**；`ProcessFrame` 的 MODE 恒拒绝、STOP 仅回复、CONTROL丢弃，`MakeStatus` 写 DISABLED/epoch0/bit0及bit3固定0。P3b 应扩展现有 can_app，而不是新写一套独立 CAN 堆栈。

已存在：`ControlCommandSnapshot {source:uint enum, generation:uint32, issued_tick:uint32, ttl_ticks:uint32, valid:bool, pan_us:int32, tilt_us:int32}`；`ControlSession {authorized, authorization_used, terminal, has_generation, last_generation, fault, expected_source}`；`ControlSession_Accept` 校验来源、32bit半环代际、TTL(<=100)、timestamp、脉宽范围；`ControlSession_Close` 必然terminal。P3b新增 `CONTROL_SOURCE_REMOTE_CAN=3`，不改变已有源枚举值。

## 3. 双向短临界区邮箱（建议实现契约，可根据当前工程命名）

用短 `taskENTER_CRITICAL()/EXIT` 复制静态结构，不要另建动态 queue/消息框架：

**CAN → Control（CanTask唯一写者 / ControlTask唯一消费者）**：

- `RemoteTargetSnapshot`: `epoch:uint16, seq:uint16, generation:uint32, received_tick:uint32, pan_us:uint16, tilt_us:uint16, valid:bool`。完整覆盖快照；只接受正确 epoch / 新 seq / 有效范围的真新帧。模半环 seq 检查在 CanTask 执行，但 ControlTask 必须再次检查 epoch、接收时刻是否晚于模式进入边界以及 age<100ms。
- `ModeRequestEvent`: `{requested_mode:uint8, action_id:uint16, expected_epoch:uint16, ...payload或指纹}` 与 `pending`。仅一条待处理，**不能被后续请求覆盖**。第二条请求若前一条尚未消费应明确拒绝，而非覆盖造成乱序成功；RK端模式请求序列化。
- `stop_pending:bool`：独立于目标/模式覆盖结构的锁存标志；收到合法 STOP 优先置位，即使 action旧也置位；另存最新STOP action供状态反馈。不清除致命fault。对同一窗口多个合法STOP保持幂等，避免后续普通控制覆写 STOP。
- `rx_overflow_pending`：一位独立事件；P3b 由ControlTask处理，要求恢复到无PWM禁用状态并失效epoch/旧目标。

**Control → CAN（ControlTask唯一写者 / CanTask唯一读取者）**：

- `ControlStatusSnapshot`: 线上状态state、reason、flags所需的 actuator_enabled、当前epoch、last_action_id、action_accepted、mode-entry/fence标记；CanTask 加入 heartbeat/IMU/bus-off等原模块可读信息，组装8字节STATUS。动作仅在 ControlTask 真正处理后才标接受/拒绝；避免 CanTask旧 P3a 的提前回复误报成功。
- 重要：不要把原 `ControlDiagnostics.mode` 枚举数值直接放入 STATUS `state`，两个枚举不等价；为 `ControlDiagnostics` 增加明确 P3b mode 或映射函数，遵守原历史枚举值兼容。

如果实际实现有更简单的等价结构也可，但只能使用**一套**原子快照/事件方案，不可并行实现队列和多套复制缓存。CAN RX 原P3a静态队列继续保留。

## 4. 时序与冲突消解

- **事件优先级**：已有 fatal fault / Actuator fault（最高且不可清除） → `stop_pending`（GLOBAL）/KEY0 STOP锁存 → RX overflow（保守Disable） → 模式请求 → 当前模式新目标/本地IMU目标 → 周期输出。普通CAN heartbeat失效不停止LOCAL。
- STOP在ControlTask本周期优先读取并处理，**先Disable再丢弃目标、递增epoch、关授权、写STATUS**；若已DISABLED则不重复递增epoch。并发 MODE_REQUEST/目标必须作废并禁止STOP之后同周期重新Enable。
- 0x181新动作序号采用16bit半环；新语法合法的请求即便epoch错误也消耗动作fence；相同ID同payload重复不再次切换，可关联缓存的最近一次结果；同ID不同payload/旧ID拒绝。以最后已处理动作ID反馈；不要让非法格式刷新门槛。
- 远程控制新 `seq` 由 CanTask 维护，每次新合法帧生成**单调增加的32bit generation**，绝不把16bit seq直接塞入 `ControlSession_Accept` 的32bit半环生成号。ControlTask 仅当看到**未消费的**generation才调用 Accept；两轮读到同一帧时不再次调用；可在100ms内按现有末次目标缓步推进。收到无效帧或重复seq不得刷新 `received_tick`。
- 每次模式改变与从非DISABLED STOP 均递增16bit epoch (mod 65536)，清除前一epoch远程 seq 的接受基线和待应用缓存；**只有进入新epoch后、实际接收时刻晚于边界的新帧**能启动REMOTE，防止队列滞留的预测epoch帧穿越切换。无跨重启防重放承诺。
- 模式变化/重新使能需要PRIMING>=40tick + 原DWT约束，首个PWM中位。REMOTE 速率状态每次从1500us重置，每个20tick步进最多±2us；最终的Pan/Tilt安全范围仍由 `Actuator_Apply` 约束，不改驱动。

## 5. 启动与本地离线授权（与六项批准相容的收口规则）

- 冷启动 `STATUS=DISABLED, epoch=0`、PWM LOW；即使CAN断开，KEY0在IMU就绪、稳定松开后新按下可使本地授权成立并转WAIT_LOCAL/PRIMING_LOCAL（首次由DISABLED进入本地时epoch+1）。不要求RK的MODE_REQUEST才能使用本地跟随，否则与“保留本地、心跳丢失不影响本地”目标冲突。
- MODE_REQUEST(LOCAL) 进入WAIT_LOCAL后仍必须等**新KEY0边沿**授权；MODE_REQUEST(REMOTE) 不需KEY0，但必须新epoch控制帧。KEY0在ACTIVE_REMOTE不隐式抢走远程源，须先MODE_REQUEST切换或STOP后重新本地按键。
- 不允许在仍按住KEY0、重启前的旧按键事件、或REMOTE停止前的键状态中自动重授权。需要新的 release→press 过程。
- 启动DISABLED状态下MODE_REQUEST(DISABLED)是同模式确认，不得自动增加epoch或使能。LOCAL/REMOTE目标相同的请求仅确认，不重置活跃会话、epoch、seq或目标。

## 6. 新生命周期API建议（非强制命名）

仅新增足够的策略函数，比如 `ControlSession_DisarmNormal()`（只处理 normal authorization，要求驱动已Disable、fault==NONE、terminal==false；重置每次授权 generation 对比，不清除故障）、`ControlSession_AuthorizeRemote()`、原 `ControlSession_AuthorizeLocal()` 扩展正常多会话许可。`ControlSession_Close(fault)` 保留终止/故障的原语义给致命分支与历史P1/P2测试；不要把正常STOP塞进会永久terminal的Close路径。保留P1 TEST单次授权兼容。完整行为由 ControlTask 决定，Policy只负责授权和命令验证。

## 7. P3b 实际集成接口（2026-10-17）

- 邮箱整体放在 `control.c`，没有新增 `.c`、任务或 RTOS queue，Keil 工程不用增项。CanTask 调用 `Control_PostRemote(epoch,seq,pan,tilt,received_tick)`、`Control_PostMode(mode,action,epoch,received_tick)`、`Control_PostStop(action)`；这些函数仅校验/投递，不操作 Actuator。`Control_GetStatus(ControlStatusSnapshot *)` 提供整组短临界区复制。
- `ControlWireState` 明确固定线上0..7，与历史 `ControlMode` 分开。状态快照包含 `state/epoch/last_action/reason/enabled/action_accepted/remote_fresh/boundary_tick/remote_tick/revision`；最后三个时间/版本字段不上线。CanTask 再按当前 tick 校验 bit3，并合成原 IMU/heartbeat/ESR flags。`revision` 只用于裁决完成后的事件 STATUS，重复动作也可触发；原 RX queue 最长等待改为20tick、周期 STATUS 仍100tick，没有新增TX队列。
- 一条最新 remote、一个 pending mode、一个 sticky STOP。另一个 latest busy-rejection 标记只保留 action，不替换首个 pending mode，也不是动作队列；突发 STATUS 允许合并。action fence 在 CanTask 调用的投递函数内推进；最终接受/缓存结果仅由 ControlTask 完成。STOP/overflow/TTL/实际切换会清旧目标、seq基线、待处理请求及重复动作缓存，保留单调 generation 与 action fence。
- 实际禁用后采样 boundary tick；新目标必须在该边界之后接收（同tick保守拒绝），ControlTask再次核对epoch/边界/TTL。远程同generation不重复调用Policy，100ms内可继续按±2µs追近；Enable/Apply提交前再次检查TTL和ISR overflow。等待REMOTE未收到首个合法目标时不凭空超时。
- `CanApp_TakeOverflow()` 是 ControlTask 的任务上下文接口，短临界区读取/清除原 ISR 溢出字；IRQ、FIFO0 callbacks和队列不变。已锁存 fault 优先；仅已授权 LOCAL 依赖 IMU 健康，REMOTE不受IMU失败或普通心跳丢失影响。
- `ControlSession_DisarmNormal()` 不清 `authorization_used/terminal/fault`。`authorization_used` 专用于 TEST 单次许可；正式 LOCAL/REMOTE 正常Disarm后可重新授权。`Close()` 仍永久terminal，驱动latch未改。默认 `P3B_OUTPUT_STAGE=0`，Stage1=无负载PWM；Stage2还需 `P3B_LOADED_APPROVED=1`，本轮未选择，构建脚本仅接受0/1。历史P2b输出开关保持0，旧LocalFollow映射参数直接复用。
