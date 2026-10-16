# P1a Implementation Plan v1.0

此计划是 Codex 在**用户已经批准的设计范围内**执行的文件边界，不授权修改旧仓库、扩充 P1b、自动推送或在实机未经验证时宣告 ACCEPTED。

## 公共前置

- 读取根目录适用的 `AGENTS.md`（如有）和本包 `README.md / requirements.md / design.md / interfaces.md / test_plan.md`。
- 记录 `git rev-parse HEAD`、`git status --short`、已有改动清单。禁止 `git reset --hard`、强制清理、覆盖用户本地改动。
- 使用用户现有 `Projects/RTnode.uvprojx`；**不因该文件未被 Git 跟踪就另建项目**。本地工程访问不到时，仅报告执行环境缺少 Projects 路径，用户把项目文件放入工作区后继续；不得推定配置错误。
- 新仓库远程历史参考 SHA：`f4d358c65dd661a8cee2541a3588939aed3ccf1c`。与当前 HEAD 不一致时记录并评估，不擅自回退。

## C1 — 工程启动与构建基线（串行第 1 个对话）

### C1.1 核对本地项目引用（只核对，必要时最小修复）

检查：`Projects/RTnode.uvprojx` 的 MCU、ARMCC 5.06u7、target、`STM32F103xE`、Flash=512 KiB/SRAM=64 KiB、Startup/system 文件仅一套、HAL include/source、绝对路径引用、输出路径和 MAP 选项。不得无理由调整既有工作正常的工程结构、优化参数或创建第二套工程。

**允许修改**：`Projects/RTnode.uvprojx`（必要且先记录原设置）；`.gitignore`（仅如需排除构建产物）；构建说明文件（可新增）。不要自动修改 `.uvoptx`，也不要提交 Objects/Listings 产物。

### C1.2 建立最小 main 启动路径

`Core/Src/main.c` 当前 `main()` 为空。建立启动初期关断引脚、`HAL_Init()`、NVIC 优先级分组、72 MHz 时钟候选（实物 HSE 确认以用户既有目标设置为准）、错误返回时持续硬件关断的框架。P1a 不调用 `vTaskStartScheduler()`；不存在 `Actuator_InitSafe()` 时先确保最小程序没有未解析引用。

**允许修改**：`Core/Src/main.c`；必要时添加一份极小公共安全/故障源文件与头文件（名称和缘由要记录）；如无必要不改 `Startup/`、`system_stm32f1xx.c`。

### C1.3 清理历史中断依赖

从 `Core/Src/interrupts.c` 的实际编译版本中清除 `protocol_transport.h` 与旧 `USART2_IRQHandler()` / `ProtocolTransport_UartIrqHandler()` 依赖；P1a `SysTick_Handler` 应只保留本阶段所需的 HAL Tick 处理，不得继续引用 `FreeRTOS.h`、`task.h`、`xTaskGetSchedulerState()` 或 `xPortSysTickHandler()` 而导致未集成内核时的编译/链接依赖；P1b 再重新审查 HAL Tick 和 FreeRTOS Tick 的协同。检查已选源文件中是否还有同类旧依赖；**不迁入整个旧协议模块**。Startup 向量表中的 USART2 向量项保留。

**允许修改**：`Core/Src/interrupts.c` 及为解决明确重复符号/必要异常而最小修改的工程配置。

### C1.4 C1 Build & handoff

用实际目标和 UV4 批构建功能编译、链接；输出完整 Build Log、MAP、内存大小，记录 ARMCC 版本与构建命令。`UV4.exe -b Projects/RTnode.uvprojx -t "<从本地工程读取的目标名>" -o <日志路径>` 仅作为命令**模板**，不得填造 Target 名称或成功日志。

通过条件：基线最小程序成功链接、所有必要中断符号可解析、无旧协议依赖、不能主动输出 PWM；若有本地 SWD/串口等可用运行观察，记录实际结果；否则明确“编译通过，尚未上板”。C1 提供 SHA/diff/文件改动报告，**C2 等 C1 验收后开始**。

**暂停**：目标工具链不在执行环境、.uvprojx 无法访问、用户本地路径引用无法解析、必须改冻结硬件资源、涉及未获批准的文件覆盖。先报告，不猜测替代工程。

## C2 — TIM3 PC6/PC7 安全驱动迁移（串行第 2 个对话）

### C2.1 复核 C1 和旧执行器代码

在 C1 最终工作区继续；读取旧 SHA：

- `Visionarm_MCU/App/actuator/include/actuator.h`
- `Visionarm_MCU/App/actuator/src/actuator.c`
- `Visionarm_MCU/doc/timer_pwm_design.md`
- `Visionarm_MCU/doc/safety_state_machine.md`

记录旧文件来源 SHA 和逐项修改意图。**不能修改旧仓库**。

### C2.2 迁入仅有的执行器模块

建议新增 `App/actuator/include/actuator.h`、`App/actuator/src/actuator.c`，保持六个 API，迁移所有正常+异常 GPIOA/PA6/PA7 触点到 GPIOC/PC6/PC7；使用 AFIO full remap；处理 SWJ 配置的风险；TIM3 时钟、PSC、ARR、OC 预加载/CCR 一致更新；加入故障锁存/新授权/恢复中位规则。

**允许修改**：新 actuator 文件；如必须，`Core/Src/main.c`、极小故障源、`Core/Inc/` 相关头文件。不得修改未来预留引脚、算法、完整 FreeRTOS 配置。

### C2.3 最小、默认关闭的测试入口

测试行为须能够**在未连接舵机**的状态下按明确授权执行：初始化保持低→使能中位→Pan/ Tilt 独立边界样例→双轴更新→Disable→再次显式授权后中位→故障关闭→锁存拒绝 Enable。避免上电自动循环全幅扫动；测试入口与正式应用隔离，默认关闭，交代如何切换且怎样恢复默认关闭。

**允许修改**：`App/p1a_test/` 下新建的极小源（若必要）；`Core/Src/main.c` 受控接入；已有 `Projects/RTnode.uvprojx` 必要的源文件列表。测试不得调用旧协议、gimbal 或 FreeRTOS。

### C2.4 C2 静态/编译检查

逐项搜索实际源文件，核对 PAN/TILT 无 `GPIOA`/`PA6`/`PA7`/`IOPAEN` 残留；审查全 remap、GPIOC CRL、TIM3 CCR/CCER/CEN/UDIS/预加载与故障路径。实际编译、链接并提交日志/MAP/diff；建立给用户的测试固件使用说明，指出哪个程序默认不使能、怎样授权，明确刷写错误版本时如何安全停止。

**Codex 不能代替用户完成**：逻辑分析仪 PWM 实测、无负载启动瞬态、上电电气状态、机械带载测试。Codex 报告最多到“准备进入实机验证”。

**暂停**：锁存无法与 API 兼容、SWJ/AFIO 有不可规避冲突、发现必须改变 PC6/PC7 分配、首次使能有已知未关闭的毛刺风险、无法解释 HAL 驱动寄存器副作用。停止并报告阶段负责人，不擅自改架构。

## 集成所有权与交付

C1 与 C2 **不可并行**：二者都可能修改 `Core/Src/main.c`、`Core/Src/interrupts.c` 和 `Projects/RTnode.uvprojx`。建议 C1 为工程基础负责人，C2 在已确认 C1 基线上只做有限增量；C2 负责最终完整构建与文件 diff 审查。Codex 不自动 push，不动旧仓库，不提交目标机器私有设置或编译产物。

完成后使用 `handoff_template.md` 报告，用户完成 `test_plan.md` 后由阶段 ChatGPT/用户判 Gate；不得自动启动 P1b。
