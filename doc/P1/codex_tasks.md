# P1a Codex Tasks v1.0

## 拆分策略

建议 **2 个串行 Codex 对话（C1 → C2）**，而非多个并行任务。原因：`main.c`、`interrupts.c` 和 `Projects/RTnode.uvprojx` 可能被两个步骤依序调整，共享全局资源不适合并行更改。Codex 完成代码和构建验证之后，用户再执行逻辑分析仪及带载验收。不要启动 P1b。

## C1：工程基线与安全启动

- **目标**：保留已有的单一 Keil 工程，恢复最小 main/时钟/安全关断启动，清理旧 USART2 残留依赖，建立可复现的编译证据。
- **前置**：用户已批准 P1a 核心设计；本地可访问 `Projects/RTnode.uvprojx`。如 Codex 环境无法访问用户本地 Keil，先明示工具限制和所需路径，不另造工程。
- **必读**：根 AGENTS（如有）、README、requirements、design、implementation_plan、test_plan、interfaces。
- **修改范围**：`Core/Src/main.c`、`Core/Src/interrupts.c`、已有 `Projects/RTnode.uvprojx`（仅必要时）、最小错误安全支持文件、`.gitignore`（仅必要时）、必要的构建说明。不创建完整控制/通信模块。
- **并行**：否，必须先于 C2。
- **验收**：真实 Build Log/MAP、编译/链接无旧模块依赖；主函数路径默认不产生 PWM；记录具体 target、工具链、SHA、diff。缺少工具时明确 NOT_RUN。

**C1 可直接复制启动提示词：**

> 你是 RT-Node F103 P1a 的 Codex C1。先阅读仓库根目录适用的 AGENTS.md（若存在）及 `P01a_actuator_safety_codex_package/` 中的 README.md、requirements.md、design.md、interfaces.md、implementation_plan.md、test_plan.md；仅执行 C1。用户本地 `Projects/RTnode.uvprojx` 已存在且确认为正确单一 Keil 工程，先使用它，不另建工程，不把其未上传 GitHub 当作错误。先检查实际 HEAD 与工作区，再最小化完成安全启动和旧依赖清理；有构建条件则使用 Keil ARMCC5 编译，记录真实日志/MAP。不得执行 C2、修改旧仓库、推送或覆盖用户文件。遇到硬件/冻结约束冲突立即暂停。结束时提交 diff、证据、未运行项和 C2 前置判断。

## C2：TIM3 执行器安全迁移

- **目标**：选择性复用旧 `actuator.c/.h`，全部迁移至 PC6/PC7 full remap，保证默认禁用、故障锁存、限幅及恢复中位，提供默认关闭的最小测试入口，准备无负载/机械测试。
- **前置**：C1 源码和构建基线已交付。若 C1 构建暂不可执行，C2 不得将结果写为已验证；是否仅做静态编辑须先由用户决定。
- **必读**：同 C1，另读旧仓库固定 SHA 的 actuator.c/.h 与两份安全/PWM 文档，查看 C1 的实际 diff。
- **修改范围**：新增 `App/actuator/include/actuator.h`、`App/actuator/src/actuator.c`、少量 `App/p1a_test/` 测试源，已存在 `Core/Src/main.c`、`Projects/RTnode.uvprojx` 的最小受控增量及必须的 HAL 关联设置。**不得**引入旧 Gimbal、USART2、FreeRTOS 任务、传感器和通信。
- **并行**：否，在 C1 后执行。
- **验收**：实际代码审查、PC6/PC7 映射和 SWJ 安全、双通道 CCR 行为、构建日志、供用户执行的逻辑分析仪测试操作、详细 diff/来源追踪。不能宣告波形/机械通过。

**C2 可直接复制启动提示词：**

> 你是 RT-Node F103 P1a 的 Codex C2。先阅读根目录适用的 AGENTS.md（若存在）、`docs/P1` 的全部相关文档、C1 实际交付，以及旧 VisionArm-MCU 在固定参考 commit 下的 `App/actuator` 文件。只执行 `implementation_plan.md` 的 C2：保留已批准的单一 Keil 工程，局部迁移 actuator 六个 API 至 TIM3_CH1/PC6 与 CH2/PC7，全重映射、默认输出禁止、紧急关断、故障锁存、双轴安全限幅和恢复中位；测试入口默认关闭。审查 AFIO_MAPR 的 SWJ_CFG 写入风险及全部失败路径。编译并保存真实日志（若环境具备），整理给用户的 H01–H10 实测指导。不要修改旧仓库、不实现 P1b、不推送、不声称已经进行硬件测量。冲突先暂停报告。

## 最终集成/实机 Gate

C2 对最终代码、工程依赖、完整构建和 diff 负责。用户完成 test_plan.md 中 H 类测试并提供原始波形/证据之后，再填写 `handoff_template.md` 判定 P1a。**不要因为 Codex 文本写“完成”就自动标记 ACCEPTED。**
