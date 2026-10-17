# Codex 对话安排（建议 C0 + C1 + C2 串行）

## C0：确认 P1a Gate 与真实工作树（立即可做）

- 目标：记录 Case1–3 的用户确认、补足 H10 状态、整理最新本地自动测试版与远端 `7588842c` 差异，不改固件。
- 必读：本包 README、requirements、P1a_gate_record、当前 P1a C2_handoff、实际 `App/p1a_test`、用户最新 Case 自动测试实现。
- 修改范围：仅本包的 P1a 记录/总结，用户授权后可保存交接文档。不可写源码。
- 并行：否；C1 以 C0 Gate 为前置。
- 提示词：

> 请读取 `P01b_freertos_control_codex_package/` 的 README、requirements、P1a_gate_record 和现有 P1a 交接。核实当前本地 HEAD 与自动 Case1–3 测试版，并保护本地未提交更改。用户报告 Case1–3 波形符合预期，记录为用户观察结果，不编造数值。询问/核查 H10 实机受限机械测试和 Build Log 状态，提交 P1a Gate 结论。**本次不修改任何源代码，不开始 P1b C1/C2**；如 H10 未通过，明确 PARTIAL 并给用户最简补齐步骤。

## C1：FreeRTOS 最小静态调度基线

- 前置：C0 确认 P1a ACCEPTED（至少编译/无负载/受限机械核心 Gate 达标）；若未达标停留设计审查。
- 修改范围：`Core/Src/main.c`、`Core/Src/interrupts.c`、`Core/Inc/FreeRTOSConfig.h`、`Core/Src/freertos_static.c`、必要的新 RTOS hook 源、唯一 `Projects/RTnode.uvprojx` 的依赖项（得到授权后）；不可改 P1a actuator 驱动。
- 并行：否。
- 验收：静态 Idle & task 对象，唯一 ARM_CM3 port，正确 SVC/PendSV/SysTick，HAL Tick，故障路径，默认 PC6/PC7 disabled，真实 build/map/hardware evidence。
- 提示词：

> P1a Gate 已由用户确认达标。请先读根目录适用 AGENTS.md 与 `P01b_freertos_control_codex_package` 全部要求，再逐文件审查实际工作树。**仅执行 C1**：使用单一 Keil ARMCC5 工程集成 FreeRTOS V10.4.6 静态调度、正确 Cortex-M3 RVDS port、SVC/PendSV/SysTick/HAL Tick、异常和 Stack Overflow Hook。保留最早安全关闭、Actuator_InitSafe 和默认禁止输出；控制任务先只运行无输出。优先复用现有代码，不覆盖 P1a 自动测试。给出 diff、最小编译源清单、完整 Build Log/MAP 或 NOT_RUN、无负载验证步骤与未解决风险。不得启动 C2、不得擅自 push。

## C2：20ms ControlTask 与无 SWD 时序验证

- 前置：C1 实际编译/无负载通过；确认可用于 LA 的非冲突 TRACE pin 已经用户明确批准（或另一替代证据方案批准）。
- 修改范围：`App/control/*`、必要 `App/p1b_test/*`、最小 C1 共享文件增量；不改 P1a PWM 驱动，不碰冻结资源。
- 并行：否；与 C1 共用核心文件，不可同时编辑。
- 验收：唯一控制所有权、20 ticks、只允许 TEST 命令、时间戳/新鲜度/generation、迟到安全关闭、无舵机自动测试、trace>=1000周期、worst execution time、stack/Flash/RAM、故障后旧命令不复活。
- 提示词：

> 在已提交且验证的 P1b C1 基线之上，仅执行本包 `implementation_plan.md` 的 C2。先读 requirements/design/interfaces/test_plan 与现有安全驱动合约。建立**静态 ControlTask（20 ticks）** 和最小单写者 TEST 快照源，保证默认禁止、显式授权、首帧等待、失约/过期安全关闭、故障锁存后旧命令不恢复。提供**无 SWD、无需 UART 输入**的有界无舵机测试镜像；和已有 P1a autotest 宏互斥。真正测 ControlTask 周期/执行时间须用用户已批准的独立 trace/外部证据，不得拿50Hz PWM代替任务周期。保护本地工程与 P1a 已测试源码。输出编译/资源/测量方案/可执行测试步骤和交接，缺实测写 NOT_RUN；不自动启动 P2 或 push。
