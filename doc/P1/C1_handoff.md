# C1 交接：最小安全启动与旧依赖清理

日期：2026-10-16（Asia/Shanghai）。状态：**PARTIAL，源码与静态检查已交付，待用户编译和上板验证**。仅执行 C1，没有执行 C2/P1b。

## 基线与用户决定

- 开始及结束 HEAD：`f4d358c65dd661a8cee2541a3588939aed3ccf1c`（P0）；没有创建 Git commit 或推送。
- 初始 `git status --short`：`?? Projects/`、`?? doc/`；已跟踪文件初始无差异。未发现根目录或父级适用的 AGENTS.md；`AGENTS_ROOT_PROPOSAL.md` 未作为生效规则。
- 已阅读本包 README、requirements、design、interfaces、implementation_plan、test_plan，以及 codex_tasks、handoff_template。
- 用户明确要求 Codex **不执行 Keil ARMCC5 编译**，编译测试、真实 Build Log/MAP 由用户提供；因此未执行任何编译器、链接器、烧录或硬件测试。
- 用户确认：工程是在 GitHub 提交后创建；忽略工程配置差异，继续推进。保留现有单一工程，不新建、不改写 uvprojx/uvoptx，不把未跟踪状态作为错误。
- 用户在本次对话中确认外部晶振 **HSE=8 MHz**，据此实施文档中的 HSE×9 时钟方案。

## 实际文件变更

| 文件 | C1 变更 |
|---|---|
| `Core/Src/main.c` | 首条语句执行寄存器关断；按实际时钟寄存器更新 CMSIS 时钟值；HAL/NVIC/Tick 初始化；HSE×9、HCLK=72 MHz、APB1=36 MHz、APB2=72 MHz、Flash latency=2；检查返回值和软件计算时钟；初始化后常驻等待，无使能路径 |
| `Core/Src/interrupts.c` | 移除旧 USART2 协议包装和 FreeRTOS Tick 依赖；SysTick 仅 HAL_IncTick；异常统一进入关断后不返回的错误路径 |
| `Core/Inc/p1a_safety.h` | 极小公共关断/致命错误声明；实现留在 main.c，无需增加 .c 工程条目 |
| `doc/P1/C1_source.diff` | 两个修改源码与新增头文件的完整补丁；不含交接文档自身 |
| `doc/P1/C1_static_evidence.txt` | 实际静态检查结果、源码及工程 SHA256、未运行项 |
| `doc/P1/C1_handoff.md` | 本交接记录与用户构建说明 |

没有修改 HAL、CMSIS、Startup、FreeRTOS 配置/源文件、个人调试配置、旧仓库或其他用户文件。未迁入任何旧执行器文件，旧代码复用溯源表本次不适用。

## 安全路径及源码核对

1. `P1a_ForceSafeOutput()` 不调用 HAL/RTOS、不等待锁或 Tick：开启 TIM3 总线时钟并读回，清 CC1E/CC2E、CEN；开启 GPIOC 时钟并读回；BRR 先写 PC6/PC7 低，再只修改对应 CRL nibble 为通用推挽 2 MHz，最后 DSB。
2. PC0–PC5 配置保持原值。没有 GPIOA、AFIO_MAPR/SWJ 写入，没有 PWM 初始化或启动、CCR/ARR/PSC 写入。
3. `P1a_FatalError()` 先硬件关断，再屏蔽可屏蔽中断，循环重复关断，不恢复正常执行。NMI/HardFault/MemManage/BusFault/UsageFault 以及本阶段不使用的 SVC/PendSV/DebugMon 进入该路径。Startup 对应 weak 符号保持不变；USART2 向量与 weak fallback 保留。
4. 本仓库 CMSIS 的 `SystemCoreClock` 初值为 16 MHz，而复位 SystemInit 使用 HSI；在 HAL_Init 前执行 SystemCoreClockUpdate，避免沿用初值配置 Tick。
5. 本仓库 HAL_Init 与 HAL_RCC_ClockConfig 都忽略内部 HAL_InitTick 的返回值。本次在初始化和时钟切换后显式检查 HAL_InitTick 结果；时钟错误保持输出禁止，不回退到另一套运行时钟。
6. 初始化结构体使用普通 `{0}`，没有复合字面量；声明放在块首，未引入依赖 C99 的循环变量声明。ARMCC5 兼容性仍需用户真实编译确认。

这是 **C1 无执行器使能入口** 的故障停留策略，不是 C2 actuator 故障锁存实现。C2 必须将其与 Actuator_ForceSafeOutput/故障锁存统一审查，不能新增可绕过关断的恢复路径。

## 本地工程只读记录与构建交接

唯一工程绝对路径：`F:\嵌入式Linux学习资料\RT-Node\Projects\RTnode.uvprojx`。

实际 XML：单一 target `Target 1`，设备 `STM32F103ZE`；Flash `0x08000000` / `0x80000`（512 KiB），SRAM `0x20000000` / `0x10000`（64 KiB）；`uAC6=1`；宏及 include 字段空；源条目 0；输出与 Listing 路径均为 `.\`；MAP 选项 `AdsLmap=1`，HEX `CreateHexFile=0`。Cpu 的 `CLOCK(12000000)` 是项目字段记录，不能作为实物晶振测量证据。按用户指示，这些差异不阻塞本次源码交付，不自动改写工程。

工程 SHA256：`0D843C27AB597F7053BCDA55BF91B9241AD8CB05BFD2877AC73766FA3D87060E`，与修改前一致。

用户在**同一工程**检查实际构建输入时，C1 所需清单如下（这是交接清单，Codex 未代为更改工程）：

- 宏：`STM32F103xE`、`USE_HAL_DRIVER`；HSE_VALUE 应为 8000000，源码对不同值设置编译期拒绝。
- 相对 Projects 的 include：`..\Core\Inc`、`..\Driver\CMSIS\Include`、`..\Driver\CMSIS\Device\ST\STM32F1xx\Include`、`..\Driver\STM32F1xx_HAL_Driver\Inc`。
- 必需源码：`Core/Src/main.c`、`Core/Src/interrupts.c`；唯一 `Startup/startup_stm32f103xe.s`；唯一 `Driver/CMSIS/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c`；HAL 的 `stm32f1xx_hal.c`、`stm32f1xx_hal_rcc.c`、`stm32f1xx_hal_cortex.c`。
- 此阶段不选入 `freertos_static.c`、FreeRTOS 内核/port/heap、HAL timebase 模板、其他 startup/system 模板、旧 UART 协议或完整 VisionArm 应用。它们没有从仓库删除。
- `p1a_safety.h` 的两个函数由 main.c 提供，不需要另外添加安全源文件。
- 文档基准工具链为 ARM Compiler 5.06u7；当前工程实际选择如上记录。Codex 未运行或切换工具链；用户实际使用的编译器版本必须由日志记载。

用户可在 Keil 中执行 Rebuild，或参考下面**未执行的命令模板**。UV4 路径和日志路径需替换为用户本机实际值；target 名来自本地 XML：

```powershell
& '<用户实际 UV4.exe 路径>' -r 'F:\嵌入式Linux学习资料\RT-Node\Projects\RTnode.uvprojx' -t 'Target 1' -o '<用户选择的完整 Build Log 路径>'
```

请保留完整 Build Log、MAP、AXF 的实际路径、时间和 SHA256，记录编译器版本、固件对应的 Git SHA **及本次源码 diff/哈希**。HEAD 尚未包含这些工作区修改，仅记 HEAD 不足以标识固件。HEX 当前未开启，无需为 C1 强行开启。ROM/RAM 占用等待真实 MAP，本次不估算。已有输出目录里的历史产物不作为本次证据。

## 实际验证与未运行项

| 项目 | 结果及边界 |
|---|---|
| S01 | 已记录 HEAD/status；没有历史重写、覆盖工程或旧库修改 |
| S02 | 已只读记录本地 XML；未证明实际工程构建输入/Startup/system 唯一性；用户已决定忽略差异继续源码工作 |
| S03 | NOT_RUN：用户明确保留编译职责；本次 Build Log/MAP/AXF/HEX 和资源统计均未取得 |
| S04 | main/interrupts 无旧协议和 RTOS 运行时引用；freertos_static.c 作为 P1b 素材保留且应排除于 C1 构建 |
| S05 | C1 默认关闭与失败/异常关断已做源码静态核对；没有执行 MCU/host 编译测试；C2 latch/Enable/Apply 行为未实现、未测试 |
| S06/S07 | NOT_RUN：full remap、PWM preload、CCR 同帧更新属于 C2 |
| H01–H10 | NOT_RUN：没有上板、波形或机械测量 |
| diff | git diff --check 通过；新头文件包含在 C1_source.diff 中；未暂存、未 commit |

静态证据由 Python 文本检查和掩码算术检查生成，包含异常 weak 符号核对、现有头文件宏名称核对。它们不代表预处理、编译、链接或硬件执行成功。

供用户 H01/H02 观察的**预期值，均非实测**：常驻主循环时 `SystemCoreClock=72000000`，HAL Tick 递增；`TIM3->CCER & 0x11 == 0`，`TIM3->CR1 & 1 == 0`，`GPIOC->CRL & 0xFF000000 == 0x22000000`，`GPIOC->ODR & 0xC0 == 0`。先不接舵机，测 PC6/PC7 无 PWM。错误路径应不返回且维持上述关闭条件。

## C2 前置判断及剩余边界

- **C1 源码可供用户编译验收；C1 完整验收尚未完成，C2 未获执行。** 用户取得真实构建/链接证据、确认无旧依赖后再决定进入 C2；若仅做静态 C2，需按 codex_tasks 的前置说明由用户另行明确决定。
- 本次继续执行的授权覆盖了本地工程差异；没有放宽冻结引脚、输出默认禁用、时钟架构或故障安全约束。
- 软件首次执行关断之前仍可能为复位高阻；逻辑分析仪显示低不能证明这一阶段为主动驱动低。
- 若 HAL Tick 不运行，HAL 的时钟等待可能无法超时；关断在进入 HAL 前已执行，无等待使能路径，但启动完成仍需要用户检查 Tick 和时钟。
- HardFault/NMI 关断为软件尽力而为；损坏时钟/总线或异常栈可能使软件不能执行。此行为不是物理制动或工业急停。
- P1a Gate 只能记为 PARTIAL；缺少 C2、构建和 H 类实测，不能判 ACCEPTED，也不能启动 P1b。
