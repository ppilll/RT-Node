# P1b 实施计划（依赖串行、无并行抢写）

## C0：P1a 准入与基线记录（先执行，**不修改固件**）

- 读取本地 `git status`、HEAD、本地自动测试 Case1–3 具体实现及 build 产物，确认与远端 `7588842c` 的差异。不得 `git reset --hard`/rebase/覆盖用户测试版；需用户补充 H10 是否完成，现有编译结果真实时间、工具链、相关异常。
- 在 `P1a_gate_record.md` 只记录 CASE1–3 为 operator reported，缺数值不造；H10 未做则 P1a PARTIAL。Codex 可以给用户简短机械测试步骤，但不得强制重新测试已有波形。
- 完成 P1a Gate 后提交适用交接信息。若 H10 未完成，暂停正式 C1/C2 编码并提交设计建议；允许用户明确将非带载的源码准备独立审查，但不能称 P1b 正式通过。

## C1：RTOS 最小工程 / 默认不输出

前置：用户/阶段负责人确认 P1a Gate；参考项目现有文件和文档。

允许修改：`Core/Src/main.c`、`Core/Src/interrupts.c`、`Core/Inc/FreeRTOSConfig.h`、`Core/Src/freertos_static.c`（仅必要）、新建最小 fatal/RTOS hooks 源头、现有 `Projects/RTnode.uvprojx` **仅在真实链接依赖需要且用户许可时最小修订**。禁止更换工程。

1. 加入最少 kernel 文件及唯一 RVDS/ARM_CM3 port；ARMCC5/CMSIS/Startup 唯一。
2. 保留最早 PC6/PC7 禁止输出、HAL 72MHz 初始化与 Actuator_InitSafe 成功路径。
3. 配置 SVC/PendSV/SysTick，谨慎处理 HAL 和 FreeRTOS 双 Tick，审查 `HAL_InitTick` 改时钟前后时序。
4. 创建唯一静态最小 ControlTask 占位任务但**不启用 PWM**，使 Scheduler 稳定运行。`vApplicationStackOverflowHook`、`AppFatal_Assert` 等真实实现；若任务创建、调度或异常失败则进入硬件安全关闭。
5. Rebuild/Link -> MAP/size；最小无舵机上板验证 tick/任务运行。无调试器时不能用 Watch 作为必需验收；可用已批准测量 pin 做 pulse。
6. 记录改动 diff、工具链日志、资源和已测证据。若不能编译，原因准确写 NOT_RUN 或 BLOCKED，先停止不编造成功。

## C2：ControlTask 20ms / 有界测试源 / 时序证据

前置：C1 提交并通过无负载测试；用户批准 trace 引脚或替代测量方案。禁止与 C1 并发编辑共享文件。

允许修改：优先新建 `App/control/*`、`App/p1b_test/*`，必要时增量修改 C1 共享 `main.c`、`FreeRTOSConfig.h`、uvprojx，**保留 actuator 驱动/自动测试代码**。

1. 定义最小 CommandSnapshot，TEST 单写者，source/valid/generation/ttl，以及 mode/重新授权失效语义。只实现 TEST；正式固件无需远程入口。
2. `ControlTask` 20 ticks `vTaskDelayUntil`。默认 DISABLED。不让 ControlTask 等 UART/I2C/CAN/SPI。为首帧、迟到、超时、长运行、故障门禁设计分支。
3. 无舵机测试镜像用编译宏一次性运行中位/受限小幅脉宽变更，结束禁止输出。与现有 P1a autotest 宏互斥，正式模式默认关闭。
4. trace GPIO 若经用户批准，短 pulse 标记唤醒/函数退出；LA 捕获 >=1000 周期，用于 interval/jitter、实际运行时间；记录时间基准、采样率、采样数、负载和测试阈值。若没有 trace，P1b 的时序 Gate 不能凭 PC6/PC7 PWM 波形 PASS。
5. 内部可统计 DWT CYCCNT（需硬件功能/读出验证）；任务栈 high water 与 MAP Flash/RAM 资源；测试失败路径（API拒绝、故障、恢复旧命令失效）。
6. 源码审查、编译、逻辑分析仪实测，保存独立结果；无带载验证前不声明 P1b ACCEPTED。

## 文件所有权与停止条件

- 核心共享 `main.c/interrupts.c/FreeRTOSConfig.h` C1 主责，C2 串行小改。
- 不允许其他任务修改 TIM3/PC6/PC7；不动 P0 冻结资源。
- 发现 SPI/CAN/UART 资源使用、AFIO重映射变更、异常入口冲突、安全恢复绕过、跨任务竞争，应立即暂停报告。
- 不自动 push / 不丢本地未提交修改；请求授权前不强制提交。
