# P1a Design v1.0 — Build, TIM3 remap & safety

## 1. 证据分级与设计边界

- **CONFIRMED（远程源码）**：新仓库 2026-10-16 的 `main` 为 `f4d358c...`；`Core/Src/main.c` 只有空 `main()`；`Core/Src/interrupts.c` 包含 `protocol_transport.h` 并调用旧 USART2 Handler；`freertos_static.c` 有 Idle task 内存提供函数；`FreeRTOSConfig.h` 设静态分配与 1 kHz Tick；Git 不跟踪 `.uvprojx`。
- **USER-CONFIRMED**：本地 `Projects/RTnode.uvprojx` 已存在并正确配置；有逻辑分析仪；舵机独立供电，开发板与云台共地；批准本包核心安全与复用设计。
- **DECIDED**：冻结 MCU、TIM3/PC6/PC7、50 Hz/1 µs、安全限幅及不自动使能。
- **NOT YET MEASURED**：Keil 构建日志、实际时钟、PWM、启动瞬态、机械动作；旧工程的所谓实测不能替代新工程结果。

## 2. 工程与文件布局

保留用户本地唯一 `Projects/RTnode.uvprojx`；Codex 只修正客观可复现的增量问题，**先检查本地实际目标与 include 路径，不另建第二套工程**。建议新增 `App/actuator/include/actuator.h` 与 `App/actuator/src/actuator.c`；必要时新建最小 P1a 测试入口源码（例如 `App/p1a_test/`），但不得在默认启动中自动使能。保留核心目录结构 `Core/`、`Driver/`、`Middlewares/`、`Startup/`。不要无差别升级或替换库。

编译基准：`STM32F103xE`；Flash base `0x08000000`, 512 KiB；SRAM base `0x20000000`, 64 KiB；Startup 使用 `Startup/startup_stm32f103xe.s`（ARMASM），system 使用唯一 `system_stm32f1xx.c`；ARMCC5、`HAL_RCC`, `HAL_GPIO`, `HAL_TIM` 等实际所需源码。代码必须兼容 Keil/ARMCC5 的 C 标准设置；旧文件的 `(T){0}` 复合字面量在当前目标编译开关下若不支持，修正或调整受控构建设置并记录，不隐瞒问题。

时钟**候选**：实物确认 HSE 为 8 MHz 时，HSE×9 → SYSCLK/HCLK 72 MHz，APB1=36 MHz，TIM3 输入时钟=72 MHz（APB1 分频不为 1 时 Timer clock×2）。`HAL_RCC_ClockConfig` 后正确维护 `SystemCoreClock`；校验 TIM3 时钟整除 1 MHz，失败进入安全态。不要仅凭 `system_stm32f1xx.c` 的 `SystemCoreClock` 初值推断时钟配置成功。

P1a 的 `Core/Src/interrupts.c` 不能残留原有 `xTaskGetSchedulerState()` / `xPortSysTickHandler()` 的运行时引用；本阶段 SysTick 先满足 HAL Tick，PendSV/SVC 的 RTOS 映射推迟到 P1b。P1a 暂不添加 FreeRTOS `tasks.c`/`port.c`/`heap_x.c` 到最小构建，除非本地目标实际另有必须保留的构建依赖且可解释。FreeRTOS 源码可继续放在仓库中。保留其配置供 P1b 后续评审。

## 3. 最早可行安全启动

推荐序列：

1. Reset_Handler → SystemInit → C runtime → main（**软件无法在 MCU 尚未执行初始化指令时保证 PC6/PC7 主动为低**）。
2. `main` 中尽早执行寄存器级安全关断：使能必需 GPIOC/TIM3 总线时钟，关闭 CC1E/CC2E 和 CEN，先预置 BSRR/BRR 为低再将 PC6/PC7 配置为通用推挽低；不得依赖尚未初始化的 HAL/RTOS。
3. `HAL_Init()`、合适的优先级分组、`SystemClock_Config()`；任一失败保持关断并进入受控错误路径（不可出现 HAL 初始化失败后输出）。
4. `Actuator_InitSafe()` 复核关闭→校验时钟→配置 TIM3 与 AFIO→初始化 CCR1/CCR2=1500，ARR=19999，预加载→仍保持输出禁止。
5. `DISABLED` 常驻；仅显式的、默认关闭的测试授权入口才可 `Actuator_Enable()`。不要在 main 初始化完成后自动使能。

PC6/PC7 复位期间的高阻/浮空特性和舵机对无信号时的实际行为取决于芯片/板级电路，不能把复位状态称作主动低电平。如果发现复位毛刺风险，记录为剩余硬件风险，不擅自增加硬件需求。

## 4. TIM3 初始化与重映射

硬件映射：`AFIO_MAPR.TIM3_REMAP[11:10]=11`（full remap），CH1→PC6、CH2→PC7；PC6/PC7 属于 GPIOC_CRL 中 6/7 号 nibble（位 [27:24], [31:28]），AF_PP 与 GPIO 输出低必须分别正确配置。

**AFIO_MAPR/SWJ 注意**：STM32F1 `AFIO_MAPR.SWJ_CFG` 的读回不是安全的可复制值；对整个 `MAPR` 进行未经控制的 RMW（包括直接滥用某些 HAL remap 宏）可能影响 SWD/JTAG。P1a 应在只有必要 remap 的受控初始化阶段，以**明确保留调试端口的 SWJ 配置**及已知其它位配置来构造 MAPR 写值，或采用经逐项审查的等效安全写法。不得盲写破坏其它 remap，也不得未经批准禁用 SWD。完成后用独立可读的 TIM3_REMAP 位验证映射，调试端口保持可用。

假定 TIM3CLK=72 MHz：PSC=71，ARR=19999，PWM1、active-high、OC preload、ARR preload；每 1 µs 计一次，周期 20,000 µs。**这是设计值，须波形实测。** HAL 具体 `HAL_TIM_PWM_Init`、`HAL_TIM_PWM_ConfigChannel`、`HAL_TIM_PWM_Start/Stop`、更新事件在本仓库版本中的寄存器副作用须逐项核查。避免在 AF 引脚切换时输出首次异常高脉冲。先关通道与计数器、装 CCR、清/同步必要状态、再受控使能；记录首次输出相位及瞬态测试。

两通道运行中 `Apply` 应写入 CCR preload，使两路在同一次定时器更新事件一起生效；不能简单认为顺序写两次 CCR 总是原子的。核查旧版 `TIM_CR1.UDIS` 保护手法及 update event/OC preload 事实，必要时用短临界段与明确同步策略；**不可为了原子更新阻止硬件停止关断路径**。P1a 不引入中断驱动 PWM。绝不配置 PA6/PA7 为执行器 PWM；其 SPI1 功能为后续保留。

## 5. 状态、安全与权限

内部最小状态：`UNINITIALIZED → DISABLED → ENABLED`，任意状态遇关键错误进入 `FAULT_LATCHED`。其中：

- 冷启动与正常初始化后默认 `DISABLED`，PWM 不输出，PC6/PC7 软件配置为低。
- `Enable` 仅在初始化且无故障、显式测试授权时允许；预置两路 1500 µs 后输出。
- `Apply` 仅 `ENABLED` 状态可以写；负值与超范围目标被分别限幅，记录 requested/applied 与 saturation。
- `Disable` 应幂等，实际硬件关断，两路拉低；不能清除 `FAULT_LATCHED`，也不把旧目标保存在下一次启用路径。
- `ForceSafeOutput` 硬件关断为第一优先级，不等待 HAL、锁、任务或通信；紧急语义要同步一个轻量**故障锁存**标志，不能只断硬件却报告仍 `enabled=true`。避免在 HardFault/IRQ 中使用不能保证安全的复杂快照复制。P1a 允许把故障锁存恢复限定为 **MCU 复位**，不用增加未批准的复杂 FaultClear API。
- 普通 `Disable` 后如后续明确再次授权，可 `Enable`；必须先重置为中位和新命令语义，禁止重放旧脉宽。
- 异常处理中的直接寄存器关断是**尽力而为**（例如电源/时钟/MCU 完全失效时软件不可保证），不可宣传为电气急停或机械制动。

实现建议：将无需 HAL 的底层“切断 TIM3 CCER/CEN + GPIOC 低”集中于一个 internal 原语；正常 Disable 与紧急 Fault 路径通过不同包装决定是否锁存。六个公开 API 名字保留，但可根据上述故障语义对实现做局部调整。`Actuator_GetSnapshot` 应显式组合/反映锁存状态；单核上的复制与 ISR 交错需采用短临界区或清晰的失败容忍契约，不能默认整个结构体赋值天然原子。

## 6. 旧代码迁移清单

来源 SHA `92d8f45bc9c7df55f6e2abea4b4abfeb090a3e3d`：

- `Visionarm_MCU/App/actuator/include/actuator.h`：保留宏与 API，移除 PA6/PA7 专用注释，明确故障锁存/快照语义。
- `Visionarm_MCU/App/actuator/src/actuator.c`：迁移 `GPIOA/PA6/PA7/IOPAEN/CRL` 到 `GPIOC/PC6/PC7/IOPCEN/CRL`（包括**所有紧急路径**）；由默认映射改 full remap；改进初始化顺序、硬件关闭与软件状态一致性、重复 Init 和 recovery 行为、CCR 双通道更新与输出瞬态。
- 不迁移 `Visionarm_MCU/App/gimbal/src/gimbal_control.c`、`gimbal_task.c`、旧 UART/通信应用。软件限幅来自 actuator，控制逻辑不能混入 P1a。

## 7. 风险及暂停条件

- **STOP**：发现本地 `uvprojx` 与用户预期目标明显冲突、必须新增第二套工程才能构建、需要变更冻结引脚/安全策略/时钟架构、PA6/PA7 与其它外设发生冲突、逻辑分析仪出现未授权 PWM。
- **STOP**：发现 AFIO/SWJ 被意外关闭、CCR 预加载并未实际开启、故障关闭存在未经控制的重新使能、依赖完整旧 VisionArm 协议才能构建。
- **记录但非自动阻塞**：本地 `.uvprojx` 尚未跟踪 Git；记录本地工程路径、构建目标和运行结果即可。是否提交源项目文件由用户决定；编译生成的 `Objects/`、`Listings/`、个人 `.uvoptx` 不必提交。

## 8. 参考

ST RM0008 AFIO_MAPR/TIM3/GPIO/TIM 一般定时器章节；本仓库实际 HAL GPIO EX 与 HAL TIM 源码；旧 `timer_pwm_design.md`、`safety_state_machine.md`；以上硬件安全选择须随代码 diff 自查。
