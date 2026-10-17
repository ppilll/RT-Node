# RT-Node F103 — P1b FreeRTOS 最小实时控制 Codex Doc 包
版本：v1.0-preflight；编写日期：2026-10-16；用途：**P1b 编码准备与条件式实施**。

## 快速结论

- 本地已有单一 Keil 工程 `Projects/RTnode.uvprojx`，不可创建第二套。
- GitHub 远程 `main` 读取基线：`7588842c29bfc0d1f5fce3011c727b4df38fc3d5`（2026-10-16 查询）。**用户后来运行的 Case1–3 自动测试版未在该远程 HEAD 中出现**。Codex 必须核对真实工作区 / 最新本地 commit；优先保护用户已经编译验证的本地改动，不允许重置覆盖。
- 用户报告 Case1–3 的逻辑分析仪波形符合预期：记录为 **OPERATOR_REPORTED_PASS（未收原始波形，范围按实际 Case 测试定义）**，不可把报告编造成具体 min/avg/max。
- 用户尚未报告 H10 受限机械运动测试、完整 Build Log / MAP。P1a 暂定 PARTIAL。**在 H10 安全确认之前，不执行 P1b 带载测试；正式 P1b Gate 在 `P1a ACCEPTED` 后开放。** C0 负责短验收补全、更新状态；本包现在即可给 Codex 阅读与规划，不强行让用户重复 Case1–3。
- 若用户明确提供 P1a H10 / 编译证据，更新 Gate 后可依序执行 C1、C2；未满足时 Codex 只做 C0 和不带载的设计核对，不声称 P1a ACCEPTED。

## 目标

在 P1a 执行器安全驱动基础上，完成 Cortex-M3 / ARM Compiler 5 的 FreeRTOS V10.4.6 最小静态调度系统，以及一个唯一正常执行器控制所有者 `ControlTask`，目标周期 20 ms。验证启动、异常、节拍、静态内存、故障停机、恢复新鲜度。外设 IMU/CAN/UART DMA/ADC DMA 等不在范围。

## 必须先读

1. 适用的根目录 `AGENTS.md`（如有），不得由此包无条件覆盖。
2. `requirements.md` → `design.md` → `interfaces.md`。
3. `implementation_plan.md` → `test_plan.md` → `codex_tasks.md`。
4. `P1a_gate_record.md` → `handoff_template.md`。
5. 真实仓库 `Core/Inc/FreeRTOSConfig.h`、`Core/Src/main.c`、`Core/Src/interrupts.c`、`Core/Src/freertos_static.c`、`Core/Inc/p1a_safety.h`、`App/actuator/`、`App/p1a_test/`、唯一 Keil 工程、旧代码交接及新增自动测试版。

## 预期两段串行 Codex 编码（Gate 满足后）

- C1：FreeRTOS 构建、异常向量、静态启动、默认禁用；实机无负载。
- C2：静态 ControlTask、20ms 测试控制源、时序/资源诊断、无 SWD 测试；无负载先行。共享 `main.c`、`interrupts.c`、`FreeRTOSConfig.h` 由 C1 负责主配置，C2 在 C1 提交后增量修改，禁止并行抢写。

Codex 不自动 push、不改旧 VisionArm-MCU；构建和上板验收由真实用户操作并记录。工具不可用填写 NOT_RUN。

## 重要已冻结资源

- TIM3_CH1→PC6 Pan，TIM3_CH2→PC7 Tilt，AFIO TIM3 完全重映射。
- 50 Hz；计数 1 µs；Pan 1000–2000 µs；Tilt 1200–1600 µs；中位 1500 µs。
- 不占用 SPI1 PA5/6/7，I2C2 PB10/11，I2C1 PB6/7，CAN1 PA11/12，USART1 PA9/10，ADC1 PA1；如需额外测量标记引脚，**先核实板级分配并请用户批准**。

## 官方资料

- [FreeRTOS Cortex-M3/M4 中断优先级](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4)
- [FreeRTOS vTaskDelayUntil](https://freertos.org/Documentation/02-Kernel/04-API-references/02-Task-control/02-vTaskDelayUntil)
- [ST RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- 最终以当前仓库 FreeRTOS v10.4.6 port 和 HAL/CMSIS 实际源码/API 为准。
