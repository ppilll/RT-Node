# P2b 最小架构与状态机

## 1. 从数据到舵机

`ICM20602 → SPI1 → P2a ImuTask(10ms) → 静态完整姿态快照 → ControlTask(20ms) → LOCAL_IMU来源校验 → 相对角/死区/限角/限速 → Actuator_Apply → TIM3/PC6&PC7`

P2a 不改变；不允许 ImuTask 直接控制 Actuator；不创建新的任务。

## 2. 状态机（建议最小实现）

- `DISABLED`：默认编译镜像始终如此、无 PWM；如 P2b 试验宏有效则可进入 `WAIT_ARM`，仍无 PWM。
- `WAIT_ARM`：仅当 IMU READY 且 `valid` 且 `now - sample_tick < 40 tick`，且 KEY0 在 READY 后经历松开→按下的稳定边沿（2个Control周期、候选40ms），KEY1未按才消费一次授权。上电已按住的 KEY0 不得自动生效。
- `PRIMING`：锁定当前 Roll/Pitch 为参考；验证新鲜本地快照/来源后 ControlTask 调用 `Actuator_Enable()`；首次输出中心；等待至少40tick及原 DWT约束，保留驱动 UIF 校验；不能立即 Apply。
- `LOCAL_FOLLOW`：每20tick读姿态，检查状态/有效期/序号/有限映射；唯一调用 `Actuator_Apply()`，记录必要诊断。
- `COMPLETE`：KEY1 主动停止，调用 `Actuator_Disable()`，会话永久 terminal；不报虚假故障。
- `FAULT`：失效/超时/驱动/调度问题，沿用 `Actuator_ForceSafeOutput()` + terminal/latch；不自动恢复。

建议不提供运行中重置会话及重新授权 API；即使用户松开又按 KEY0，也不允许在同一启动生命周期重新输出。若以后真要重复使能，留到单独总控评审。

## 3. 映射候选（测试前参数，非机械校准结果）

- 使能参考角：`roll0`, `pitch0`；每次跟随 `dr=roll-roll0`,`dp=pitch-pitch0`。初始化和参考采集需要静止、设备大体水平；避免 ±180° 角跳变。
- 方向：`PAN_SIGN=+1`、`TILT_SIGN=+1` **只是编译期候选**，必须无负载测方向后可改 -1。不同模块安装方向改变正负关系。
- 死区 `2.0°`；输入限角 `±15.0°`；增益 `3 µs/°`；目标 `1500 ±45 µs`（Pan/Tilt均成立），显著小于现有驱动软限幅。
- 每 20 ms 最大变化 `±2 µs`，对应100 µs/s 脉宽变化率；从中心到+45µs约0.45s。`target = clamp(center + sign*3*clamp(deadband(relative),-15,+15), test_min, test_max)`，再对 `last_output` 做 `±2µs` 的速率限制，最后由驱动再次限幅。
- 初始 `last_output=1500µs`；第一次有效命令不跳步、不从之前测试命令接续。
- 这些参数是便于安全验证的初始试验值，不代表机械角度比例或转动速度已经实测；改动必须记录。

## 4. 新鲜度/序号与时间边界

- `Imu_GetSnapshot()` 成功读取 + `state==IMU_READY` + `valid==true`，`(uint32_t)(now-sample_tick) < 40U`；所有时间戳为1kHz 32位 tick，按模数计算。
- 限制与状态必须在每次 Apply 前检查，不能把 FAILED 时残留 `sample_tick` 当作新数据。
- `generation` 采用 uint32 半环新旧比较；ImuTask 发布每帧+1，ControlTask每20tick消费最新帧，跳号允许，重复/倒退拒绝。请勿使 P1 TEST 原有生成号语义回归。
- `issued_tick=sample_tick`、命令 `ttl_ticks=40`，来源显式 `LOCAL_IMU`；不能用 ControlTask 当前时间“刷新”陈旧样本。
- 在 arm/prime/active 阶段异常时关闭；未授权前 IMU 尚未 READY 只等待，不因等待就开启舵机。

## 5. 按键授权和停机

- 候选：KEY0 PE4(active LOW) 使能，KEY1 PE3(active LOW) 停止，内上拉，20ms 控制循环轮询即可；硬件参考资料指出该板通用按键连接如上，Codex 必须检查当前实物与占用。
- KEY0 必须在 READY 后人工明确新按下，不能上电按住触发；KEY1 优先于 KEY0。去抖候选2次20ms检测。KEY1 释放后不重新武装。
- 按键是人为停机手段，不是经认证的硬件急停；首次带载仍须可立即切断独立舵机电源。
- 默认固件 `P2B_OUTPUT_STAGE=0`：任何按键动作都不启动 PWM。阶段1：无舵机信号/供电、编译明确授权的 PWM 验证；阶段2：**仅在 G2 通过后由用户明确批准**、有界机械测试。阶段宏及 ACK/KEY 必须使未授权组合编译失败。
- 强制 `P1A/P1B` 输出测试宏关闭，禁止模式共存；不实现 CAN 仲裁。

## 6. 重要接口设计决策

P1 `ControlSession` 只认可 TEST 并且一次授权。保留 `terminal/authorization_used` 和驱动 latch，进行 **最小来源扩展**：会话绑定 `expected_source`，原 `Authorize()` 可保持作为 TEST 向后兼容接口；新增显式 `AuthorizeLocal()` 或枚举授权，不改变 P1 TEST 行为。不能让未知来源绕过校验。

P1 ControlTask 内原 `#if P1B_AUTOTEST_ENABLE` 分支是有限测试，P2b 增加独立 `#elif P2B...` 路径；不能以全局状态重置实现“启动”或清除终止状态。

## 7. 实时性/资源

ControlTask优先级保持1，ImuTask保持0；无充分证据不改优先级。P2b新增每20ms少量GPIO读、临界区快照、浮点乘法与限幅，检查 ControlTask 原2ms执行窗口。静态构建MAP与主机时序仿真不能代替真实 WCET；没有批准的读出通道时标 `NOT_MEASURED`。P2b不加复杂GPIO trace。
