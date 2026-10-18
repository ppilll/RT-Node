# P3b 需求与验收边界

## 功能需求

**R1 控制源**：只允许 LOCAL_IMU 或 REMOTE_CAN 一个源正常输出；`ControlTask` 为唯一正常写者。已有 `CONTROL_SOURCE_NONE=0 / TEST=1 / LOCAL_IMU=2` 保持数值，新加 `CONTROL_SOURCE_REMOTE_CAN=3`。

**R2 模式**：内部至少能表达 DISABLED、WAIT_LOCAL、PRIMING_LOCAL、ACTIVE_LOCAL、WAIT_REMOTE、PRIMING_REMOTE、ACTIVE_REMOTE、FAULT。`STATUS.state` 使用 `doc/P3a/interfaces.md` 定义的 0..7，不得直接把原 `ControlMode` 枚举数值作为线上 state。

**R3 LOCAL**：ICM20602/SPI1/ImuTask 不改；保留 KEY0 PE4、KEY1 PE3（低有效、两轮20ms消抖）、READY/valid/样本年龄<40ms、`LocalFollow_Start` 重新标定基准与目标 ±2µs/周期。KEY0 必须有 READY 后重新松开再按下的明确边沿，不能上电按住触发；每次 NORMAL STOP 后也须新边沿。**没有 CAN 心跳时 LOCAL 仍应工作**。

**R4 REMOTE**：MODE_REQUEST REMOTE 被 ControlTask 接受，先 Disable、切换 epoch、进入 WAIT_REMOTE；必须在新 epoch 收到新 seq/范围合法且未超时目标才 Enable，经过>=40ms PRIMING，随后每20ms按 ±2µs/轴追随目标；最终经 Actuator_Apply() 限幅。实际 REMOTE_CONTROL 周期建议20ms，合法命令TTL100ms。

**R5 STOP**：正确的 `0x182` Global STOP 对所有正常模式有效，优先于普通控制/模式请求；KEY1 在 LOCAL/REMOTE 都可以停止。先 `Actuator_Disable()` 再废弃目标/授权、进入 DISABLED。STOP 对 FAULT 不能解锁，不能用过期 action_id 拒绝 STOP。即使 STOP 发生在 WAIT/PRIMING 也须废弃目标并禁止后续自动使能。

**R6 正常重授权**：正常 STOP/REMOTE_TIMEOUT 是 DISABLED（不伪造致命故障）；远程恢复必须新 MODE_REQUEST + 新 epoch + 新控制 seq；本地重启动需新 KEY0 边沿。致命驱动/调度/时钟故障继续 `Actuator_ForceSafeOutput()` + terminal/latch，只能复位恢复，严禁 `memset(ControlSession,0)` 或清除驱动 latch。

**R7 协议兼容**：所有帧11-bit、IDE0/RTR0、DLC8、Little Endian；CAN IDs为0x180/0x181/0x182/0x183/0x280。MODE 的action_id与STOP共享序列门槛，MODE 拒绝应报告对应状态；STOP不因旧action拒绝。epoch切换使旧帧无效；没有新seq不得以读取同一快照刷新 TTL。心跳不能当目标命令。

**R8 STATUS**：100ms+事件，提供真实 state/reason/flags/epoch/last_action。MODE 的成功与否必须由 ControlTask 最终决定，不得由 CanTask 根据报文合法性就发送虚假成功；状态帧非阻塞，BUS-OFF 时允许发送失败，不能影响正常控制；P3b `flags.bit3` 按新鲜远程目标且模式/epoch正确给出，bit0按真实 Actuator enabled。

**R9 溢出/总线错误**：P3a 的 RX overflow 已记录但不影响执行器。P3b 若硬件FIFO/软件队列丢帧，安全地禁用当前运动并使旧命令不可恢复（保持 reason=0x04）；正常重授权仍需要新模式/KEY0。REMOTE 命令超时按100ms停止；单纯 HEARTBEAT 缺失不得无故停止 LOCAL。CAN Bus-Off 若控制 REMOTE 且没有新控制，应最终由100ms TTL 禁用；不应因 Bus-Off 自动误清本地会话。

**R10 阶段输出门禁**：采用一处明确的 P3b 输出阶段设置：0默认关闭PWM、1舵机信号及供电断开仅测PC6/PC7、2仅用户审核T2后显式授权受限带载。不得因为此前 P2b 曾试验通过就自动授权 P3b 带载。不要沿用 `P2B_OUTPUT_STAGE` 作为正式 P3b 运行开关；旧阶段测试宏与新模式互斥。

## 非目标

不重构 HAL/FreeRTOS；不重新做P1/P2全功能压测；不实施 CAN FD、ROS、OTA、硬件急停、加密鉴权、分包、持续性能跟踪框架；不修改现有 PWM 硬件驱动内部寄存器安全实现。

## 验收

- Keil5 ARM Compiler 5.06u7 真实完整构建成功。
- T2 断开舵机后测 PC6/PC7：两轴、中位、限速、限幅、PRIMING 与未授权禁止PWM均符合预期。
- T3 LOCAL/REMOTE 互斥，切换禁止旧命令，LOCAL 不依赖 CAN 心跳。
- T4 Global STOP、过期100ms后禁用及新授权要求正确；硬件/软件致命故障不被普通 STOP 清除。
- T2 无负载通过、操作者另行明确授权并核对机械限位和紧急断电能力后，才进行受限带载试验。没有实测结果写 NOT_TESTED；不要标 P3b ACCEPTED。
