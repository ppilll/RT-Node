# P1b C1 交接

日期：2026-10-17（Asia/Shanghai）。**C1 源码审查、补充保护及真实构建完成；上板验收 NOT_RUN，P1b PARTIAL。** 没有执行 C2，没有烧录、带载测试、Git commit 或 push。

## 基线与修改归属

- 唯一工程：`Projects/RTnode.uvprojx` / `Target 1`。本地 HEAD `7588842c29bfc0d1f5fce3011c727b4df38fc3d5`，未查询/修改远端。
- 实际文档目录为 `doc/P1b`，用户列出的八份文档及 handoff_template 均已读取。仓库、F: 根及中间父目录未发现适用 AGENTS.md；AGENTS_ROOT_DELTA_PROPOSAL.md 仅为提案，未据其建立新规则。
- 进入本轮已有未提交的 P1a 自动测试、C1 核心代码/工程配置和构建产物。全部保留；`evidence/C1/entry/` 保存进入时 Git 状态、tracked diff、相关源码哈希及旧 AXF/HEX/MAP/日志。旧产物已经是调度版，不能当作 Case1–3 历史镜像。
- 用户本轮明确授权执行 C1，按非带载源码/构建范围推进；H10 未获完成报告，P1a Gate 仍 PARTIAL，未声明正式 Gate 已满足。Case1–3 仍为 OPERATOR_REPORTED_PASS，无重复要求或新增波形数字。

| 最终 C1 文件 | 状态与实际行为 |
|---|---|
| `Core/Src/main.c` | 进入时已有安全初始化、静态 ControlTask、scheduler handoff 和测试宏拒绝。本轮增加静态分配/周期 tick 配置编译保护，首次任务检查 Thread/PRIMASK/BASEPRI、scheduler、priority group、SysTick reload 和 disabled/fault 状态。任一失败走既有不返回锁存关断。 |
| `Core/Src/interrupts.c` | 进入时已改，审查后保留。SVC/PendSV 交给唯一 RVDS port；单一 SysTick 每次 HAL_IncTick，scheduler 未启动时不调用 port，运行或挂起时继续交给 port。 |
| `Core/Inc/FreeRTOSConfig.h` | 进入时已改，审查后保留。1kHz、static=1/dynamic=0、overflow=2、无 timers、真实 INCLUDE_xTaskDelayUntil、stack high water API 支持、priority 15/5。 |
| `Core/Src/freertos_static.c` | 复用，未修改；唯一静态 Idle TCB/stack。 |
| `Core/Src/rtos_hooks.c` | 进入时已有未跟踪文件，复用；assert/overflow hook 不读取可能损坏的 task/name，调用 P1a_FatalError，硬件先关闭、锁存、不返回。 |
| `Projects/RTnode.uvprojx` | 进入时已有 C1 依赖与 ARMCC5 配置，本轮未额外编辑，原工程真实 Rebuild。 |
| `doc/P1b/C1_verify.py`、`C1_handoff.md`、Gate 记录、`evidence/C1/*` | 本轮验证脚本、交接与证据；不属于 MCU 运行时功能。 |

`App/actuator/*`、`App/p1a_test/*` 均与进入本轮时 SHA-256 一致。PC6/PC7、TIM3 full remap、50Hz、1µs、Pan/Tilt 限幅、默认禁用及故障锁存保持。未使用任何新增 GPIO 或冻结 SPI/I2C/CAN/USART/ADC 资源，没有 CAN、IMU、UART DMA、ADC DMA、命令源、完整状态机或 C2 App/control。

`evidence/C1/C1_source.diff` 为相对 HEAD 的最终 C1 源码/工程补丁，包含进入时已有修改及新增 hook；`C1_this_turn_main.diff` 单独展示本轮 main.c 增量，避免将用户已有工作归为本轮新写。

## 最小编译与启动审查

实际编译共 15 个文件：main、interrupts、freertos_static、rtos_hooks、actuator、p1a_test（测试宏关闭）、startup_stm32f103xe、system_stm32f1xx、HAL core/rcc/cortex/gpio，以及 **tasks.c、list.c、portable/RVDS/ARM_CM3/port.c**。无需 queue.c，不链接 heap_x.c、其它 port、timer 或通信源。真实 linker response 保存在 `evidence/C1/RTnode.lnp`。

最早 P1a_ForceSafeOutput → SystemCoreClockUpdate → HAL_Init/检查 HAL_InitTick → HSE 8MHz / PLL 72MHz / APB1 36MHz → Actuator_InitSafe → xTaskCreateStatic → scheduler。现有 HAL_Init 和 HAL_RCC_ClockConfig 忽略内部 InitTick 返回值，main 保留显式检查；所有 HAL 时钟超时调用均在创建任务前。创建静态任务会通过 port 临界区设置 BASEPRI；此后不依赖 HAL_Delay/时钟超时。

handoff 在 PRIMASK 关闭时停止/清 SysTick、清 pending，再由 RVDS port 配置同一硬件 tick。port 的 prvStartFirstTask 重新开 IRQ，SVC 恢复 PSP/BASEPRI；SVC/PendSV 不再误走 Fatal。1kHz HAL/RTOS 共用 SysTick，HAL 启动阶段与 RTOS 的计数原点不同，不可用绝对 tick 值相等作标准；不在运行中重新配置时钟、HAL tick 或 suspend tick。priority group=4；内核=15；FromISR API 优先级数值必须 >=5，本轮没有业务 ISR API。

ControlTask 只有首次安全检查和 20 ticks 阻塞占位，不调用 Enable/Apply/Disable，也没有测试源。20 ticks 不是实测任务周期或 C2 完成证据。Fatal/overflow/assert 无 RTOS、HAL delay 或日志依赖，保留硬件先关闭与不可返回锁存语义。

## 真实构建证据

运行时间：2026-10-17 09:34:17–09:34:21 +08:00。

```powershell
# 工作目录 Projects，仅编译，不下载
& 'D:\MKD5.36\UV4\UV4.exe' -r RTnode.uvprojx -t 'Target 1' -o ..\doc\P1b\evidence\C1\rebuild.log
```

实际通过 Hidden Start-Process -Wait 调用上述参数，退出码 **0**。MDK 5.36；ARMCC、ARMASM、ARMLINK 均 **5.06 update 7 (build 960)**；C99，工程 Optim=2（ARMCC -O1），Split Sections 开启。工程 pCCUsed=5060960，uAC6=0。

日志首行显示 `Using Compiler 'V6.16'`，这是本机注册名称与实际工具版本不一致；未隐藏该矛盾。完整 HTML 的 Tool Versions、独立 `armcc --vsn`、MAP 首行实际均为 ARM Compiler 5.06u7，且 RVDS ARMCC 汇编 port 已成功编译链接。未为更正本机注册标签而修改全局 Keil 配置。

- Rebuild：**0 errors / 0 warnings**。
- Code=6824，RO-data=360，RW-data=96，ZI-data=3528 bytes。
- Total RO=7184；**Flash ROM=7280 bytes**（Code+RO+RW 初始值）；**静态 RAM=3624 bytes**（RW+ZI），对工程 512KiB Flash/64KiB SRAM 配置。
- Control TCB `0x20000060` / 72 bytes，stack `0x200000A8` / 256 words=1024 bytes。
- Idle TCB `0x200004A8` / 72 bytes，stack `0x200004F0` / 128 words=512 bytes。
- 原 startup 保留 MSP stack=1024 bytes、C runtime HEAP reserve=512 bytes，均已计入 RAM。此 reserve 与 FreeRTOS heap 不同；未改 Startup，最终链接代码未出现 malloc/calloc/realloc/free 或 pvPortMalloc/vPortFree。栈分配值不等于已测余量。

证据目录 `evidence/C1/` 包含：`rebuild.log`、`rebuild_exit_code.txt`、`compiler_version.txt`、完整 `RTnode.build_log.htm`、`RTnode.map`、`RTnode.axf`、`RTnode.hex`、`RTnode.bin`、linker response/scatter、`final_hashes.json`。

HEX SHA-256：`E23AEEB67CE296A19EC93E2A370D328275070B937E73C9D58532FB86261C1DCA`。

AXF SHA-256：`EE99B8852C358CB980203EC704DF4AEE8912AA292545E6C42233AFD0EED3D90F`。

## 验证结果与实测边界

`python doc/P1b/C1_verify.py` 返回 **0**，输出见 `verification.txt`，真实 ARMCC 正/负向编译参数和输出见 `compile_guard_results.json`。

| 检查 | 结果 |
|---|---|
| 唯一 target、ARMCC5、最小 FreeRTOS 依赖，无 FreeRTOS heap | PASS：工程与链接检查 |
| SVC/PendSV/SysTick 唯一实际定义与最终二进制 vector slot 11/14/15 | PASS：分别指向 port.o、port.o、interrupts.o，非 weak fallback |
| 静态 Control/Idle TCB 与 stack，hook/scheduler/static create 链接 | PASS：MAP 与二进制检查 |
| 默认 main 编译，分别误开 P1a autotest/mailbox/P1b autotest | PASS：默认成功，三项分别触发预期 #error |
| 生产链接的 Actuator_Enable/Apply/Disable | PASS：无这些代码；无正常输出调用路径 |
| actuator/P1a test 文件保护 | PASS：进入/结束哈希相同；未重跑 Case1–3 |
| B01 默认低电平、B02 实际调度、B03 实际 tick/fault 路由 | NOT_RUN：本轮未操作板卡；构建/静态检查不代替实机 |
| B04/B05 周期与执行时间 | NOT_RUN：C2 范围，未批准独立 trace pin/诊断通道 |
| B08 实际栈余量 | NOT_RUN：只确认静态分配，未使用 SWD/Watch 伪装实测 |
| B06/B07/B09 输出测试、迟到/失效/恢复 | C2 行为未实现；C1 致命路径仅源码/链接审查，未做硬件注入 |
| H10 / B10 带载 | NOT_RUN：未获 H10 完成报告，也没有带载授权流程 |

无新增 GPIO，不使用 CH340/USART1 输出诊断。若仅观察 PC6/PC7 持续低，只能证明输出处于安全状态，不能证明 scheduler 存活或 20ms 周期。

## 后续人工验证与遗留问题

1. 无舵机信号及供电条件下，使用上述哈希的 C1 HEX；通过现有下载方式烧录，记录板型、下载结果和固件哈希。
2. LA 观察 PC6/PC7，上电及复位应持续低、无 PWM。保存型号、采样率、阈值、复位前后捕获；出现输出立即停止。
3. 另行核对并批准不冲突的外部观察方案，才能补 B02/B03 调度/tick 实机证据；本轮未自行指定引脚或占用 USART1。无 SWD 时 Watch 不作为必要或唯一方法。

遗留：H10/历史 P1a 构建证据未补；C1 板上调度与 tick、异常安全动作、真实栈余量未测；Keil 本机注册标签错误仍存在但实际 ARMCC5 已确认。没有发现构建错误或冻结资源冲突。

**停在 C1：源码与构建完成，上板验收待补。不得把本交接当作 P1a/P1b ACCEPTED、C1 硬件 PASS 或进入 C2 的授权。**
