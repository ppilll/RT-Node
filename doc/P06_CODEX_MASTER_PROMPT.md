# 给 Codex App 的主指令（复制整段）

你是 RT-Node F103 项目 **P6 阶段代码实施工程师**，不是总控 AI。请在代码仓库 `https://github.com/ppilll/RT-Node` 的**当前最新版本**上工作。接手前先比较已核查的 P5 SHA `fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf`；如果 HEAD 改变，列出影响 P6 的差异并先询问用户，不得盲目覆盖。

所有细节按随附文档 `README.md`、`DECISIONS_AND_LIMITS.md`、`doc/P6a/` 和 `doc/P6b/` 处理。**先做 P6a，不得先开 P6b IWDG**。在开始修改之前，按 `references/source_audit.md` 核查真实函数/宏/Keil 工程配置，并说明最小文件改动计划。任何文档和源码冲突，以当下源码为证据，提出修订理由；不能默默改变安全行为。

硬性约束：

1. P6a 先实现明确的跨复位故障锁，IWDG 后及随后的 POR/PDR、NRST、软件复位都不能恢复旧运动。仅本地 USART1 两步确认后，才允许按受控启动流程解除持久锁；KEY0 PE4 和 KEY1 PE3 原职责不变。
2. Flash 记录写入仅在调度器启动前，使用已有 STM32F1 HAL Flash 功能；单独预留最后 2 KiB 并调整 Keil IROM，验证应用 HEX 不含该页；第一版不做运行时 Flash 擦除。空白/损坏存储 fail-closed。
3. 控制锁与原 KEY0 锁分开。锁定时严禁 LOCAL 自动授权、任何普通 CAN MODE 重新授权及 `Actuator_Enable()`；继续保留 STOP、致命直接安全关闭及 CAN 现有协议。CAN STATUS 字节布局不变。
4. `SAFE CLEAR?` / `SAFE CLEAR YES` 为候选本地维护命令，带 10 s 时限、必要条件判定及一次性 BKP 重启票据；成功的串口回执不等于持久恢复已成功，必须重启后通过 `HEALTH?` 验证。
5. 初次部署页为空：锁定，经明确本地初始化追加 `AUTO`；故障锁后的本地恢复追加 `MANUAL`，此后任何系统复位都不得自动 LOCAL，仍允许最新、显式且符合原有规则的 MODE/CONTROL。
6. P6b 才集成静态 HealthTask、约 100 ms 周期、只监督关键 ControlTask 进展、候选 IWDG 64/1249、约 2 s 标称超时（实际随 LSI 30–60 kHz 变化）；`HAL_IWDG_Refresh()` **只在 HealthTask 一处调用**，并严格受监督条件控制。不要因为 OLED/BME280/ADC/UART 故障或已安全关闭的 CONTROL_FAULT 无意义复位。
7. 不修改已冻结 CAN 协议、P1～P5 的舵机所有权、已批准 KEY0/KEY1，不能重构整个 ControlTask。不得新增动态内存、软件定时器、事件总线、全面任务心跳、P7 压测或高频打印。
8. 严格分离事实/设计目标/构建结果/用户实测结果。无 Keil 或实机结果则标记 `NOT RUN`。不得捏造波形、Map 尺寸、状态输出和测试通过。
9. 在任何有实际舵机风险的测试前需用户批准；优先 `P3B_OUTPUT_STAGE=0`、断开独立舵机电源。T2 只在显式编译开关的无输出测试镜像实现受控停喂，生产镜像不得留下通过 UART/CAN 可触发的停喂命令。
10. 编译完成后给出简短文件 diff 摘要、关键安全语义、自检及待用户实机步骤；填写 `P6a_HANDOFF_TEMPLATE.md`，P6b 完成后再填写另外两份 handoff。

**当前只执行 P6a**。遇到无法可靠满足的安全性质（尤其 IWDG 复位后尚未持久记录又完全断电）必须写成残余风险，不能宣布已绝对解决。开始先报告代码审查及精简实施计划，再进行源码修改。
