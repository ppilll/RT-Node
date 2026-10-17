# P3a Codex 对话划分和执行提示词（V1.0）

## 对话划分

推荐 **一个 Codex 对话** 执行整个 P3a（新 CAN 模块 + Keil 工程改动 + 最小编译检查）。如果 RK3588 端仅用 `cansend`/`candump`，无需第二个开发对话；除非后来需要极小的重复发送脚本，才单独交付且不得并发修改 STM32 工程。

文件唯一所有者：本 Codex 对话负责新 `App/can/*`、`Core/Src/main.c`、`Projects/RTnode.uvprojx`、必要 CAN IRQ 强符号；其他对话不得同时改共享文件。

## 可直接复制给 Codex 的任务

> 仓库 `ppilll/RT-Node`，任务是 **P3a ONLY**。先读取适用 AGENTS.md（若存在）、`doc/P3a/README.md`、`requirements.md`、`interfaces.md`、`design.md`、`implementation_plan.md`、`test_plan.md` 和 P2b 文档；核对 HEAD 与设计基线 `59e4a0b0ebf738a8221f719703948e798c6d7d47`。按冻结 CAN 协议实现 CAN1 bxCAN（PA11/PA12，FIFO0，标准帧 ID 0x180..0x183），静态队列/CanTask、简短 RX ISR、非阻塞 `0x280` STATUS。Keil 增加已有 HAL CAN 源文件与 FreeRTOS queue.c。必须保持 `P2B_OUTPUT_STAGE=0`，不修改 ControlSession/Actuator/IMU，不加入 REMOTE 控制或 PWM Enable/Apply。P3a 所有 MODE_REQUEST 只能拒绝并回 STATUS，STOP 作为无动作的安全确认，HEARTBEAT 改变链路位，REMOTE_CONTROL 不驱动舵机。CAN bitrate 与时序必须来源于已确认的实测配置，不能猜；如缺失则记录 blocker，不声称通过编译/实机 T1。只进行必要编译与一组 T1 实机准备，输出 Git diff、真实构建/测试状态、`P3a_HANDOFF.md`。未经用户再次授权，不实施 P3b/P4；不要重构整个工程。

## Codex 提交前自检（最少项目）

- 所有 CAN ID / 字节 / DLC / epoch / seq 与 `interfaces.md` 一致。
- 没有 CAN ISR 中长时间协议处理、阻塞发送或 ISR 里调用普通 FreeRTOS API。
- 没有在 scheduler 启动前开启会使用 FreeRTOS API 的 RX 通知。
- 没有远程 PWM 路径；CanTask 从不调用 Actuator。
- 新旧 HAL CAN 源文件不重复编译；Keil 编译项/包含路径完整。
- 构建/实测没有证据则如实标记 NOT_TESTED/BLOCKED，不编造 PASS。
