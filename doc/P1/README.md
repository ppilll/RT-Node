# RT-Node F103 / P1a 执行器安全迁移 Codex Doc 包

**版本：v1.0 · 日期：2026-10-16 · 状态：用户已批准核心设计，待 Codex 实施和实机验证**

## 1. 任务定位

本包仅用于 **P1a：现有 Keil 工程可重复构建、TIM3 双轴 PWM 安全迁移、无负载与受限机械验证**。不是整个 P1，也不是 P1b FreeRTOS 编码包。

**执行约束：先读文档，后修改代码；Codex 只负责可以在其环境内执行的修改、构建和静态验证；真正的实机测试由用户执行。** 不允许把预期结果填写为测得结果。

## 2. 工程输入与唯一基线

- 新仓库：`https://github.com/ppilll/RT-Node`，审查用 `main` SHA：`f4d358c65dd661a8cee2541a3588939aed3ccf1c`。
- 旧仓库（**只读**）：`https://github.com/ppilll/VisionArm-MCU`，参考 SHA：`92d8f45bc9c7df55f6e2abea4b4abfeb090a3e3d`。
- 用户本地**已经存在且确认可用**的单一 Keil 工程：`Projects/RTnode.uvprojx`，相关 `Projects/RTnode.uvoptx` 为个人调试配置。此前未上传 GitHub 是仓库跟踪策略，不等于工程不存在或错误。**不要新建第二个 uvprojx，不要把「未跟踪」自动判为阻塞**。
- 平台：正点原子精英 V2 / STM32F103ZET6 / Keil MDK5 / ARM Compiler 5.06u7 / STM32 HAL；P1b 才启用 FreeRTOS 最小调度。
- 用户已有逻辑分析仪；已确认舵机独立供电、开发板与云台供电共地。旧代码注释标称 S20F，沿用旧的软件安全脉宽基线；机械安全仍在实机测试阶段验证。
- `P0` 为静态设计 ACCEPTED，**无新工程构建或实测通过记录**。

Codex 启动时记录实际仓库 HEAD 和本地工作区变更；若高于参考 SHA，仅做增量兼容检查，不得擅自回退、覆盖或重置用户文件。

## 3. 文档阅读顺序

1. 仓库根 `AGENTS.md`（若存在）；本包 `AGENTS_ROOT_PROPOSAL.md` 仅是根规则提案，不会自动生效。
2. `requirements.md`：P1a 范围、不变量与 Gate。
3. `design.md`：工程基线、GPIO/TIM3/故障安全设计。
4. `interfaces.md`：六个执行器 API 的契约、允许的调用上下文。
5. `implementation_plan.md`：分步实施、文件范围、暂停条件。
6. `test_plan.md`：静态/构建/上板测试、容差和证据格式。
7. `codex_tasks.md`：C1、C2 两个**串行** Codex 对话和可复制提示词。
8. `handoff_template.md`：记录实际产出与后续硬件验证。

## 4. 执行顺序和门禁

- **C1：基线修复与可重复编译**：以现有 `Projects/RTnode.uvprojx` 为唯一项目，整理最小启动、历史残留依赖和构建证据；保证禁用状态。
- **C2：执行器安全迁移**：以 C1 为前置，选择性迁移 `actuator.c/.h`，PC6/PC7 全重映射、故障锁存、最小受控测试入口；完成编译与静态审查。
- **用户实机测试**：按 `test_plan.md` 无舵机测波形、禁用和故障路径、之后受限机械测试。
- **P1a 验收**：以证据判定 `ACCEPTED / PARTIAL / BLOCKED / FAILED`。**P1a 未被 ACCEPTED 之前，不进入 P1b 带载调度测试。**

## 5. 冻结的资源与非目标

- TIM3_CH1 → PC6 (Pan)，TIM3_CH2 → PC7 (Tilt)，AFIO TIM3 full remap；50 Hz，1 µs 分辨率；Pan 1000–2000 µs，Tilt 1200–1600 µs，中位均 1500 µs。
- 预留 SPI1 PA5/PA6/PA7、I2C2 PB10/PB11、I2C1 PB6/PB7、CAN1 PA11/PA12、USART1 PA9/PA10、ADC1 PA1。不得占用。
- P1a **不实现** ControlTask、MPU6050、CAN、UART DMA、ADC DMA、BME280、SSD1306、完整故障健康管理。
- 软件禁用 PWM **不能称作物理制动/工业急停**。

## 6. 交付清单

Codex 每个对话结束都要提供实际 SHA、Git diff 摘要、修改清单、工具链与编译命令、Build Log、MAP、静态检查结果、未完成测试、风险和下一步；不自动推送仓库，不声称已完成用户操作。

## 7. 官方依据

- ST RM0008：`https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf`。
- STM32F103ZE Datasheet：`https://www.st.com.cn/resource/en/datasheet/stm32f103ze.pdf`。
- ST STM32F1 HAL：以**本仓库实际版本**头文件和源码为准。
- FreeRTOS Cortex-M3：`https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4`（供 P1b 前置研究，不授权本阶段 RTOS 集成）。
