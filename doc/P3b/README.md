> **P7 安全更正（2026-10-19）：本页是历史 P2b/P3b 设计/测试记录，按键授权步骤不适用于当前 P5/P6 源码。当前 KEY0(PE4)=STOP 且锁存至复位，KEY1(PE3)=OLED 翻页；不得按 KEY0 启动、不得用 KEY1 停机。控制授权服从 P6 持久锁和现有 MODE 策略。Stage1/Stage2 本轮未授权，当前操作以 [P7 现场指南](../P7/operator_runbook.md) 为准。**

# RT-Node F103 — P3b Codex 任务入口 V1.0

日期：2026-10-17。仓库：https://github.com/ppilll/RT-Node
GitHub 已读取的 P3a 基线提交：`b2c1f34595ae125a3b27f2d5143c4202762a705c`（`P3a`，本包编写时的 HEAD）。**Codex 执行时再次核对 HEAD、工作区未提交修改和适用的 AGENTS.md，不能盲目覆盖后续代码。**

## 状态及来源

- 用户在本轮明确报告：**P3a 已完成 STM32↔RK3588 CAN 双端通信实测并通过**。这是用户报告的事实，不等于我读取到了 candump/逻辑分析仪原始记录。`doc/P3a/P3a_HANDOFF.md` 仍保留上一轮 `T1 NOT_TESTED` 的历史记录；不要伪称该旧文件已更新。Codex 若收到原始 T1 记录可补交接，否则在 P3b_HANDOFF 中写 `P3a T1 = USER_REPORTED_PASS; raw evidence not provided`。
- P3a 已实现 `App/can/can_app.c/.h` 和 `can_timing.h`；CAN1 bxCAN、PA11/PA12、500 kbps、Prescaler4/BS1=14TQ/BS2=3TQ/SJW=1TQ、FIFO0、8帧静态队列、CanTask priority0/384words、NVIC6、非阻塞 STATUS。`main.c` 已调用 `CanApp_Init()`，Keil 已包含 HAL CAN、FreeRTOS queue.c。**P3b 不重做 CAN 物理通信。**
- `App/can/can_timing.h` 此时 `P3A_CAN_TIMING_USER_APPROVED=1`、`P3A_CAN_TIMING_CONFIRMED=0`；既有用户T1通过的口述不自动等于已经归档的测量证据。不得为了推进P3b而任意改动500kbps已用的时序。
- P3a 当前仍把 MODE_REQUEST 一律拒绝、REMOTE_CONTROL 解析后丢弃、合法 STOP 只回复、不改变输出；CAN 状态由 CanTask 独自构造。这几项必须在 P3b 依冻结协议接入 ControlTask。

## 已批准且不可擅自修改的 6 项 P3 决策

1. 普通 STOP/远程目标过期后可重新授权；时钟、调度、执行器等致命故障保持硬件与软件锁存、只能复位。
2. REMOTE 由明确 `MODE_REQUEST(REMOTE)` + 新 epoch 下的新鲜 `REMOTE_CONTROL` 使能；LOCAL 保留 KEY0 人工授权。
3. `REMOTE_STOP` 是 **GLOBAL_STOP**，对 LOCAL / REMOTE 都生效。
4. 允许运行中模式切换，但必须先 `Actuator_Disable()`，旧目标与授权失效，新模式需新控制数据/授权；不直接重放旧数据。
5. 协议 5 个 11-bit ID、DLC8，不改字节布局；命令 TTL100ms，心跳周期100ms、超时500ms，STATUS100ms加事件响应。
6. REMOTE 限速为每20ms每轴最多±2µs，最终仍经 Actuator 绝对限幅。

## 本次实施目标

在原 P3a 稳定通信基础上，最小扩展 `ControlSession`、`ControlTask` 和 `can_app.c`：完成 MODE_REQUEST/STOP/REMOTE_CONTROL 的跨任务投递与仲裁、正式 LOCAL_FOLLOW 可用、远程命令有效期和模式隔离、STATUS 真实状态/epoch，附最短 RK3588 测试脚本。**所有正常执行器调用仍归 ControlTask。**

阅读顺序：适用 AGENTS.md → 本 README → `requirements.md` → `interfaces.md` → `design.md` → `implementation_plan.md` → `test_plan.md` → `codex_tasks.md` → `doc/P3a/interfaces.md` 和 P2b 的 design/interfaces → 当前源码。`doc/P3a/interfaces.md` 是线路协议权威文档，本包不可另立不兼容协议。

## 范围限制

- 不修改 `App/actuator/src/actuator.c`、`App/actuator/include/actuator.h`、IMU 驱动、FreeRTOS port、HAL CAN 源文件、startup、SysTick 和 fatal handler；新功能不得在 CAN ISR / CanTask 中调用 Actuator。
- P3a 已实现的 CAN filter/IRQ/时序/收发器链路尽量不动；只修改 P3b 业务必需部分。
- P1/P2 专用测试分支不应混为新生产模式；迁移正式 LOCAL_FOLLOW 时需保留原 KEY0/KEY1、40ms PRIMING、IMU 校验、安全异常行为。
- 不增加 ROS/MQTT/复杂的网络层、通用状态机平台、动态内存、大量编译宏、重型测试工程、哈希审计或 P7 压测。
- **先编译，再舵机断开下测PWM，最后在用户明确批准后做受限带载验证。** Codex 没有设备时不能声称实机 PASS。当前用户授权的是 P3b 开发，不是自动批准带载动作或进入 P4。

## 权威参考

- 项目 CAN 字节协议：`doc/P3a/interfaces.md`
- STM32 参考手册：https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
- 当前 HAL CAN 模块：https://github.com/STMicroelectronics/stm32f1xx-hal-driver/blob/master/Inc/stm32f1xx_hal_can.h
- Linux SocketCAN：https://docs.kernel.org/networking/can.html
- can-utils：https://github.com/linux-can/can-utils
