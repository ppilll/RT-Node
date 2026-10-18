# P3b T2–T4 核心测试（精简版）

## 前置条件与证据状态

- 用户明确报告 P3a CAN 双端 T1 通过；本轮未见原始 candump，先记录 `USER_REPORTED_PASS`；必要时只复用一次新STATUS，不进行P3a整套重测。
- 用当前编译产物，核对 `P3B_OUTPUT_STAGE` 及编译日志；Stage1 测试时**舵机信号线与舵机电源均断开**，仅用逻辑分析仪测 PC6/PC7/GND。RK3588 已配 500000bit/s Classical CAN `can0`。不要在接线不明时带载发送控制。
- `candump -L can0` 记录 `0x280`；不要把RK自己发送的报文回显当STM32应答；STATUS字段以`doc/P3a/interfaces.md`为准。T2/T3/T4只保留3组测试，少量candump、LA截图和PASS/FAIL即可。
- 先记录复位时STATUS，预期无故障冷启动 `state=0 DISABLED, epoch=0, flags.bit0=0`；如KEY0已触发本地授权，先安全复位并保持KEY0松开再开始序列。IMU bit1可动态变化，不能预设。

### 终端准备

```sh
ip -details -statistics link show can0
# 需要配置时且确认不是其它进程正在使用 CAN：
ip link set can0 down
ip link set can0 type can bitrate 500000 sample-point 0.833333 fd off
ip link set can0 up
candump -L can0
```

以下帧按首次冷启动epoch0、action1、mode REMOTE所举例；**应先观察上一条STATUS的epoch再发送后续控制，不盲目硬编码epoch。**

## T2 — 无负载双轴目标及授权边界（Stage1）

1. 舵机**信号+供电断开**，逻辑分析仪PC6/PC7/GND。上电未授权时两线保持LOW、不产生有效50Hz PWM。
2. RK3588先发 `cansend can0 181#0102010000000000` （请求REMOTE、action1、expected epoch0）。等到 STM32 `0x280 STATUS` `state=4 WAIT_REMOTE, epoch=1, last_action=1, bit5=1`；不能再像P3a一样恒拒绝。MODE确认本身不应产生PWM。
3. 再发 `cansend can0 180#01000100DC05DC05`（epoch1,seq1,Pan/Tilt均1500us）；紧接着使用短Python/C脚本每20ms发送**seq递增**的相同1500µs值，避免仅一帧100ms到期引起停机。预期Enable后先中心启动、PRIMING不少于40ms、active `STATUS.state=6`；PWM周期20ms、两轴1500µs附近。
4. 把远程Pan命令轻微改为1510、Tilt保持1500；示例 `cansend can0 180#01000200E605DC05`，但实际连续脚本必须继续递增seq（不要重复seq2）。每Control周期实际目标变化至多±2µs/轴，不绕过Actuator限幅。再试Tilt 1510/1490附近（在安全范围内），确认PC7对应Tilt。
5. 发送旧epoch/重复seq/越界目标，只应忽略、不得刷新控制有效期、不得产生未经授权的大幅运动。测试单组内即可，不创建大量负向测试。
6. 验证不影响原TIM3配置（50Hz，Pan PC6，Tilt PC7）；只有LA实测才能记 `T2 PASS`。若不具备LA，记`NOT_TESTED`。

## T3 — 模式仲裁、本地离线与旧命令隔离（Stage1）

1. 从ACTIVE_REMOTE使用新action和最新epoch请求LOCAL，例如当前epoch1且action2：`cansend can0 181#0101020001000000`。ControlTask应Disable并进入`WAIT_LOCAL=1`，更新epoch为2，旧epoch1 CONTROL不得继续Apply。
2. KEY0在IMU READY后必须经历稳定松开→按下，不允许之前已经按住时自动重启。授权后STATUS先PRIMING_LOCAL=2，>=40ms后 ACTIVE_LOCAL=3；用轻微手动IMU姿态改变确认PC6/PC7是 LOCAL_FOLLOW 目标（沿用1455..1545µs映射及±2µs限速）。
3. LOCAL期间停止RK心跳至少>500ms，STATUS.flags.bit2转0，但本地有效IMU仍跟随，不能因为普通CAN链路失活停LOCAL。
4. 运行中LOCAL→REMOTE用最新epoch的`MODE_REQUEST(REMOTE)`，验证先Disable/失效本地目标，再获得新epoch的新控制帧才Enable；不允许同时由 IMU 和CAN写最终目标。
5. 可增加一次无CAN情况下KEY0从DISABLED起本地授权测试：确保本地离线能力、KEY0重授权需要新边沿；只保留一个合并测试记录。

## T4 — GLOBAL_STOP、远程过期、重授权、致命fault边界（Stage1）

1. 在REMOTE ACTIVE时（需要正常周期发送 seq），发送正确GLOBAL_STOP，示例 `cansend can0 182#0101030000000000`（action3，旧action也应有效）。下一次Control周期应先Disable，PC6/PC7 LOW，STATUS `state=0, reason=02, bit5=1`；若原非DISABLED，epoch+1。**STOP之后不得因为排队的CONTROL或MODE立即重启。**
2. STOP后继续发送旧epoch控制帧不得使能；即使CAN心跳继续，新MODE_REQUEST(REMOTE)且匹配**新epoch**，随后新epoch新seq，才能进入REMOTE。
3. 在ACTIVE_REMOTE停发新控制目标，但允许继续发送HEARTBEAT（counter每次更新），达到 `last_valid_rx_tick +100ms` 后Remote必须停止旧目标并Disable；STATUS `state=0, reason=03`，连续心跳也不能保持PWM；恢复制帧但未重新模式请求不得自动使能。注意20ms任务周期可使实际观察落在下一个周期；记录观察结果，不臆造精确最坏响应时间。
4. LOCAL ACTIVE时也发送GLOBAL_STOP，确认该STOP优先级覆盖LOCAL，用户KEY0必须重新释放→按下才可能再次本地授权。KEY1在REMOTE ACTIVE也须停止。无需在所有状态重复测试。
5. 致命故障（如driver latch、deadline）不要求本轮人为注入；审核原代码路径不会因普通STOP、DisarmNormal清除terminal/fault/Actuator latch，必要时做一个合并纯C策略主机小测试；没有硬件故障实测不要记`FAULT_RECOVERY_MEASURED`。
6. 在ACTIVE_REMOTE可选验证CAN RX队列溢出导致保守DISABLED和reason04，不要求为故障注入开发复杂生产机制；若无法受控触发，做代码审查并注明 `NOT_INJECTED`。断链情况下GLOBAL_STOP不可保证送达，REMOTE应由TTL停机。

## Stage2 受限带载（T2/T3/T4 Stage1 PASS 后另行人工批准）

用户先人工确认机构没有超过机械极限风险、可以随时**物理切断独立舵机供电**、有场地人员保护、官方跳线/供电接线无误。只从中位及小幅附近目标（例如1500→1510μs）验证一两次正常运动和STOP，避免自动大范围扫描。没有用户明确批准 **不得**让Codex直接编译/烧录stage2并自动带载执行。

## 核心记录模板（每组填写一行）

| 测试 | 操作/条件 | 预期 | 实际 | PASS/FAIL/NOT_TESTED | 最小证据 |
|---|---|---|---|---|---|
| T2 | 中位+Pan/Tilt小位移，Stage1无负载 | 50Hz、PRIMING>=40ms、±2µs/周期、互不串轴 | 待用户填写 | NOT_TESTED | 1份candump+1张LA |
| T3 | REMOTE↔LOCAL、KEY0、失心跳 | 互斥、旧epoch失效、LOCAL继续 | 待用户填写 | NOT_TESTED | 1份candump+必要LA |
| T4 | STOP、只发心跳超时、重授权 | 立即禁用旧目标、不可自动重放 | 待用户填写 | NOT_TESTED | 1份candump+必要LA |
| Stage2 | 用户人工许可后小幅带载 | 行程安全且STOP有效 | 待用户填写 | NOT_TESTED | 简述实际操作 |

> STATUS样例只用于说明字段；完整8字节输出以真实板卡为准。日志和截图不需要反复复制或计算哈希。不能把仿真/编译称为已测PWM、实时WCET或受限带载结果。
