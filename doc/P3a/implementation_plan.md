# P3a Codex 实施步骤（V1.0）

**只允许一个 Codex 实施对话写 STM32 工程共享文件。不要并发修改 `main.c`、`Projects/RTnode.uvprojx` 或中断向量。**

1. **阅读与对齐**：阅读适用 AGENTS.md（若存在），本 P3a 文档，P2b 设计与接口，本文引用源码。核对当前 HEAD 是否仍为 `59e4a0b0...` 或已有其他工作改动；记录真实差异。
2. **硬件配置门禁**：查看仓库/用户提供的历史 CAN 参数。未知波特率不能默认填 500k/250k；若未提供，先完成参数无关部分并报告待确认，勿宣称构建/实机通信已达成。
3. **实现 CAN 模块**：在 `App/can/` 添加极简静态 CAN1 HAL 初始化、FIFO0 filter、IRQ 与 callback、协议解码/STATUS 编码和 CanTask。若拆多个 .c 必须避免函数重复/多层抽象。引入一个固定深度8的静态 FreeRTOS Queue，不用 heap。
4. **线程/中断正确性**：初始化中不启动带 FreeRTOS API 的 RX 中断；CanTask 进入 scheduler 后开始 CAN 与通知；NVIC 数值6；Rx 回调只取数据/本地 tick/非阻塞投递；异常回调只记录少量状态。控制任务始终不调用 CAN API。
5. **最小工程修改**：`main.c` 增一处 CanApp_Init；Keil 添加 HAL CAN、FreeRTOS queue.c、新 CAN .c 和必要头路径。`interrupts.c` 原有 SysTick/fault/SVC/PendSV 保留，优先由新模块定义 CAN IRQ 强符号。不修改 actuator/IMU/control 源码或 P2b 编译授权开关。
6. **协议行为**：逐字段实现 `interfaces.md`；初期 STATUS 固定 DISABLED/epoch0；合法 MODE_REQUEST 拒绝并反馈；合法 STOP 幂等接受；HEARTBEAT 可刷新链路位；REMOTE_CONTROL 只能解析/丢弃。实际本地 IMU READY 状态按原接口读取。
7. **编译检查**：若已获得 bit timing，执行/指导 Keil5 ARMCC 5.06u7 构建并记录编译结果；本环境若无 Keil，不得冒充编译成功。构建无历史测试宏授权，P2B Stage0。
8. **交付 T1**：给用户最少 SocketCAN 发帧命令与 expected STATUS 字段、T1 PASS/FAIL 填写位置；用户负责实机测试。整理 `P3a_HANDOFF.md`。仅当 Keil 编译和实机双向验证确实通过，才建议 P3a ACCEPTED。

建议允许变更清单：新 `App/can/*`、`Core/Src/main.c`、`Projects/RTnode.uvprojx`；`Core/Src/interrupts.c` 仅必要时。不能借本轮同步大量历史日志、重构 HAL 或整合 P3b。

必须在总结中明确：`P3a` 可以解析控制协议但 **不执行任何远程舵机动作**。
