# P2a 具体实现顺序与文件所有权

## 唯一 Codex 工作会话

为了避免重复 PR/共享文件冲突，P2a 初期只安排 **一个** Codex 实现会话，按下列步骤串行完成。只有提交 `P2a_HANDOFF.md` 后，用户才能决定是否开第二个修复会话或实施 P2b。

### Step 0：检查仓库而不修改

1. 读取 `AGENTS.md`（如果不存在则记录，不要因 P2a 自动创建根规则）。复核当前 HEAD、git status（不得覆盖未提交改动）。
2. 必读实际代码：`App/actuator/include/actuator.h`、`App/actuator/src/actuator.c`、`App/control/control.h`、`App/control/control.c`、`App/control/control_policy.h/.c`、`App/control/control_config.h`、`Core/Src/main.c`、`Core/Src/interrupts.c`、`Core/Inc/FreeRTOSConfig.h`、`Core/Inc/stm32f1xx_hal_conf.h`、`Projects/RTnode.uvprojx`、`doc/P1b/interfaces.md`。
3. 查找 `Resources/vendor/ICM20602/` 下厂商压缩包。存在则计算 hash 并参考其 `.c/.h`；不存在则明确记为 `VENDOR_NOT_IN_REPO`，以本包和官方 datasheet 为准，不声称已经检查仓库内的厂家文件。
4. 输出简短 source baseline、资源占用和适用 `AGENTS.md` 摘要；有重大接口/安全冲突先停下询问，不擅自决定。

### Step 1：新模块（不得改 P1 控制）

- 建立 SPI1/PC4 最小驱动，CS 高闲置，SPI mode3、/128、14 bytes burst；ID 检查、复位、SPI-only、量程/DLPF/100Hz 配置、关键读回。有界失败后返回 invalid。
- 建立纯 C 姿态模块；测试向量覆盖解码、单位换算、水平静止、±Roll/Pitch、变化 dt、零偏、无效输入。
- 建立 ImuTask、静态栈/TCB、校准/单写者快照和任务上下文安全读取 API，ImuTask 优先级0（不改 ControlTask优先级1）。
- 不使用 heap、软件 timer、queue、新 IRQ，除非证明框架必须且经用户批准。

### Step 2：唯一集成修改

本单会话独占下列共享文件修改权，**最小修改**：

| 文件 | 允许修改 | 禁止修改 |
|---|---|---|
| `Core/Src/main.c` | 增加 IMU 模块头与 `Imu_Init()` 失败处理调用，置于 scheduler 前 | 时钟配置、SysTick handoff、P1 安全先行逻辑 |
| `Projects/RTnode.uvprojx` | 新增 IMU C 文件、已有 HAL SPI C 文件、必要 include 路径 | MCU/ARMCC 配置、P1 源组/源文件顺序变更、无关 target |
| `Core/Inc/stm32f1xx_hal_conf.h` | **预期无需修改**；既已启用 SPI | 整体更换 HAL 库或使能无关模块 |
| `Core/Inc/FreeRTOSConfig.h` | **预期无需修改** | 时基/优先级/动态分配策略 |
| `Core/Src/interrupts.c`, `App/control/*`, `App/actuator/*` | **只读** | 任意变更 |

预期在原工程增加 `Driver/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c`；若现有 HAL 头及库版本无法编译，先定位原因，不替换整个 HAL。

### Step 3：主机测试与构建

- 给出可直接执行的主机测试命令和结果，必须运行 `imu_fusion.c` 真实实现，不允许只用伪算法。驱动层可使用 HAL mock 验证超时失败不泄漏 CS 和不发布部分帧。
- 使用用户工程 `Projects/RTnode.uvprojx`、ARMCC5 编译并 Rebuild，保存原始完整日志、MAP、ROM/RAM 样本、项目 HEX hash。没有 MDK 工具链则 `BUILD_NOT_RUN`，不得填 PASS。
- 检查默认输出禁用、新增代码是否误调用 Actuator_Enable/Apply、P1 安全文件 diff 应为空。

### Step 4：交付给用户进行真实硬件测试

- 提交 `test_plan.md` 中的逻辑分析仪连接/解码步骤。用户自行 CH340 烧录、断舵机供电并测试，不虚构已经烧录、波形或测量。
- 整理 `P2a_HANDOFF.md`：commit/hash、实际变更文件、配置、主机/构建证据、资源、NOT_RUN 硬件事项、P1 风险与下一步。
- **停止**。不得在本 Codex 会话进入 P2b，尤其不得让舵机自动运动。

## 资源改动策略

- 不引入厂商压缩包以外的依赖和全局第三方库。若须 `libm` 相关数学符号，按 ARMCC 的实际链接规则评估且记录 Flash 增量。
- `ControlTask` 现具执行预算2ms/唤醒保护；ImuTask 优先级低，保护既有时序。未获得外部测量时不得写“实机 WCET 已通过”。
- 高风险修改（P1 actuator 安全接口、ControlSession/源优先级、GPIO 全局 AFIO map）必须立即停下并要求用户/总控批准。
