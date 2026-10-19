# P7 measurement_plan · 量化指标、精度与统计口径

## 实现口径补充（以此及P07_HANDOFF为准）

PERF3 flags bit0=active、bit1=饱和（发生超UINT32_MAX尝试）、bit2=无效观测；d为无效周期数。tick间隔>=59000、DWT禁用或delta=0拒绝计为合法样本，更新基准后继续观察。首窗入口仅建基准。P7计数饱和；旧wake/late/rx_count语义不变。CANC e是带sticky FOV0标志的错误回调次数，可能重复，并非硬件FIFO独立丢帧数；d是HAL读取失败次数；三个事件均饱和到UINT32_MAX。

PERF4保留旧ControlMode/ControlFault数值，用来观察终态，不添加任何重置/解锁/授权命令。c/i/h来自原自启动最低栈HWM。查询间非全局原子快照；每次getter自身整体复制。新增入口统计包含在原elapsed点之前；elapsed归档在旧安全判断之后，归档开销不包含在x内。

极值自开机10s开窗累计到查询，不能把主机120/600s定时边界当作精确极值起止；报告t/u/n和主机起止、尾部排空时间。没有窗口重置命令。主机发送成功/本地loopback不代表STM32处理；脚本CAPTURED也不是硬件PASS。

## 1. 统一条件

- 同一正式Stage0固件、相同软件配置、相同硬件接线、安全页保留；`P3B_OUTPUT_STAGE=0`、`P6_WDG_TEST_ENABLE=0`、旧输出测试关闭。测量期间不调节原实时优先级或超时门限。
- 72MHz Cortex-M3，1kHz RTOS tick；`DWT->CYCCNT`在当前ControlTask里使能并主动检查可运行；无ST-Link/J-Link依赖。
- 测量窗口：从ControlTask首次有效启动后经过**10,000 tick**开始采集P7数据（首次窗口周期不使用跨启动门槛的旧样本）。A、B应各自按相同条件安全重启并预热，**绝不能全片擦除安全页**。若不便安全重启，使用不修改安全状态的观测方案，并如实说明A/B极值不可严格分割。
- 标称周期=20,000us=1,440,000 CPU cycles。对相邻`uint32_t`DWT读数用无符号模差，典型20ms间隔小于32位回绕周期；72MHz下回绕周期约59.65s。任何间隔异常到无法消除整圈歧义时必须标为不可判定，不能信任模差。目标窗口期间HealthTask应发现长时间停滞，但这不等于“永远无回绕歧义”。

## 2. 最少P7性能数据结构（建议，不要求照搬命名）

| 字段 | 单位/初始/更新 | 意义 |
|---|---|---|
| `active/window_start_tick` | bool/RTOS ticks | 10s预热后本统计窗生效 |
| `period_samples` | uint32增量 | 合法的相邻入口周期样本数 |
| `period_min_cycles` | uint32，首次样本初始化 | 观测最短入口间隔 |
| `period_max_cycles` | uint32，首次样本初始化 | 观测最长入口间隔 |
| `elapsed_max_cycles` | uint32 | 入口到现有执行段计时点的最大时间，可能包含抢占/栈扫描 |
| `period_violation_count` | uint32 | 入口周期小于19,000us或大于21,000us的次数；不受`session.terminal`抑制 |
| `wake_count`/旧`late_count` | uint32 | 原有诊断，不变更 |
| `control/idle/health_min_stack_words` | words | 历史最小剩余栈高水位 |

**边界判定：** 对独立周期统计采用完整DWT分辨率和精确的 `19,000*72`、`21,000*72`；超出边界才计违例；不要把1ms tick值直接作为µs精度。已有Control故障逻辑基于19..21tick等条件，保持原样。

**执行段：** 在当前 `elapsed_cycles = DWT->CYCCNT - entry_cycle`附近更新P7 max，注意现有`SampleStack()`在该点之前。上限2,000us是现有故障门限**不是实测结果**；不能报告为理论WCET或者CPU独占占用。

**计数器边界：** 饱和或自然回绕须明确测试与表现；不要更改原wake_count等计数语义。P7新计数建议饱和并给出饱和标志（或保持自然回绕但报告不可用），避免长运行得到更小伪数值。

**采集偏差：** A(120s)和B(600s)样本数量不同；建议把窗口内样本数、名义时长和统计口径一起报告，不能把两个观测极值差直接归因于通信负载。

## 3. 推荐只读串口回复契约（可等价微调，但需有解析测试）

保留已有64B `reply`、单一 UartTask TX、分块发送且每5s低频读取；本约定全部十进制uint32取4294967295时，单行依然适配。换行CRLF、ASCII、严格整数解析，不使用`printf`动态拼接。

```
PERF?   -> P7 a=<period_min_cycles> b=<period_max_cycles> x=<elapsed_max_cycles> v=<period_violation_count>\r\n
PERF2?  -> P7N n=<period_samples> c=<control_min_stack_words> i=<idle_min_stack_words> h=<health_min_stack_words>\r\n
PERF3?  -> P7W t=<window_start_tick> u=<last_observed_tick> f=<flags> d=<invalid_samples>\r\n
PERF4?  -> P7S m=<old_control_mode> f=<old_control_fault> w=<old_wake_count> l=<old_late_count>\r\n
CANC?   -> CAN r=<processed_rx_count> q=<queue_send_fail_events> e=<overrun_callback_events> d=<hal_read_fail_events>\r\n
```

实际五条在四项均为uint32最大值时经真实C测试最长分别为**56B/57B/57B/57B/57B**（含CRLF）。CANC比建议版多一个HAL读取失败事件d，仍在64B内。`period_samples=0`时`a/b`不能输出假的周期，建议返回0并由主机依据`n=0`判为“尚无样本”。保持原 `COUNT?`/`HEALTH?` 完全兼容。

- `r`：CanTask进入 `ProcessFrame` 的次数，包含格式/内容最终不接受的帧；**不是有效命令数或物理收帧数**。
- `q`：`xQueueSendFromISR`失败次数；只能统计检测到的队列投递失败事件。
- `e`：检测到的FIFO overrun/错误通知次数；HAL错误回调可能重复，不是精确丢失CAN帧数。若无法可靠识别硬件overrun，将`e`明确标记为error callback event count。
- 不用 `CanApp_TakeOverflow()` 生成`CANC?`；它是正常安全停机路径会消费的事件标志。
- `PERF?`/`PERF2?` 属不同时间点快照，记录查询时间且允许数字轻微不一致；必要时把全部观测数据先复制到局部快照再格式化。**不能阻塞ControlTask做连续输出**。

## 4. T1真实结果计算

```
min_period_us = a / 72.0
max_period_us = b / 72.0
max_abs_deviation_us = max(abs(min_period_us-20000), abs(max_period_us-20000))
max_observed_elapsed_us = x / 72.0
period_violation_count = v
```

独立窗数据需同时记录：测试固件git SHA、Stage、开始/结束、主机时长、`n`、watchdog reset/no-reset、Control fault/状态、栈高水位words与最终Keil MAP。

**验收偏差建议：** `19000 <= min_period_us`且`max_period_us <= 21000`，`v=0`，`x/72 < 2000`，关键栈剩余>0，测试中无意外复位/故障。若不满足：如实记FAIL/边界，不许修改安全门限让数据变漂亮。

## 5. T2通讯与脚本口径

- 目标`CAN 50 frames/s`和`UART PING 10 commands/s`是输入工况，不是吞吐极限。分别记录 planned、`send()` accepted、error、真实间隔/发送时长。
- `CAN r_after-r_before`仅在窗口内单一发送端、只发合法且被滤波接受的0x183报文、停止后短时间排空及查询时序清楚的情况下，可对比主机成功send数；**不能把比值叫精确线层丢包率**。
- CAN状态帧0x280只是设备存活佐证；Linux本地loopback或发送成功不代表STM32已收到/处理。
- `UART sent_PING`、`received_PONG`在单发送者且无额外PING时可做回复计数匹配。现有协议无请求序号，所以不能凭PONG单独证明逐条端到端不重发。需分类 ADC、其他诊断、ERR、乱码/不完整行。`RX`是字节进度，`OVF`是异常事件数；不能解读成精确丢字节数。
- 实际观察到 CAN 事件、UART OVF、未预期IWDG复位或Control终态FAULT，要保存现场和真实发送速率；不要自动放大负载继续压测。

## 6. G-A/G-B/G-C 和故障证据边界

- G-A证明 `ImuFusion`、`LocalFollow_Target`和策略纯C逻辑；G-B证明板上ICM20602读数随姿态更新；G-C在**另行批准的Stage1**以LA证明两路PWM响应。
- Stage0不编译 `Motion` 的正常PWM输出部分。G-C未测不能写“云台运动轨迹准确”或“完成执行器输出闭环”。
- P6真实复位/RCC/Flash锁只引用已有实机回执；没有原始日志则写USER_REPORTED/NOT_VERIFIED。IWDG64/1249@标称40k LSI约2s不是实测。

## 7. 官方参考

- ARM Cortex-M3 TRM: https://documentation-service.arm.com/static/5e8e107f88295d1e18d34714
- FreeRTOS stack HWM reference: https://www.freertos.org/media/2018/FreeRTOS_Reference_Manual_V10.0.0.pdf
- Linux SocketCAN: https://docs.kernel.org/networking/can.html
