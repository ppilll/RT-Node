# C2 交接：TIM3 PC6/PC7 执行器安全迁移

日期：2026-10-16（Asia/Shanghai）。结论：**PARTIAL：C2 源码、静态审查与用户实测指导已交付；未编译、未上板，P1a 未 ACCEPTED。**

## 基线与执行边界

- 起止 HEAD 均为 `f4d358c65dd661a8cee2541a3588939aed3ccf1c`；没有 commit、暂存、push 或历史改写。
- 初始状态：`M Core/Src/interrupts.c`、`M Core/Src/main.c`、`?? Core/Inc/p1a_safety.h`、`?? Projects/`、`?? doc/`。已与 C1 交接/源码 diff/哈希核对；C2 在 C1 工作区上增量修改。
- 根目录及父级未发现适用 AGENTS.md；`doc/P1/AGENTS_ROOT_PROPOSAL.md` 仅为提案，没有建立根规则。用户提示 `docs/P1` 对应实际 `doc/P1`；已读该目录全部输入文档和 C1 实际交付。
- 用户本轮明确只做 C2、无需编译；授权在 C1 缺少构建证据时继续静态编辑。沿用 C1 的 HSE=8 MHz 用户确认和保留工程差异决定，没有修改冻结资源/安全限幅。
- 旧仓库本地位置 `F:\嵌入式Linux学习资料\Vision Arm\Visionarm_MCU`，HEAD 为固定参考 SHA，但有用户本地修改。迁移只用 `git -c safe.directory=<该路径> show <固定SHA>:Visionarm_MCU/...`，未使用工作区改动版本，未修改旧仓库或全局 Git 配置。
- C2 增量补丁见 `C2_source.diff`；C1 历史交接/证据保持不动，其中 C1 的“尚未 C2”是当时状态。

## 文件与旧代码溯源

| C2 文件 | 改动与来源 |
|---|---|
| `App/actuator/include/actuator.h` | 从旧 `Visionarm_MCU/App/actuator/include/actuator.h` 保留六 API、命令/结果字段和限幅宏，增 `fault_latched`，明确快照/失败语义；不沿用旧标定措辞作为新板实测结论 |
| `App/actuator/src/actuator.c` | 参考旧同路径源的绝对目标、ClampPulse 和 UDIS 双 CCR 思路；硬件子集重写为受控寄存器序列，PC6/PC7 全重映射；修正启动脉冲、锁存/重复调用、异常竞态、默认禁止、恢复中位 |
| `Core/Src/main.c` | 保留 C1 时钟/启动顺序和原始关断；致命路径统一调用 actuator 紧急锁存；时钟成功后 InitSafe；默认仅 WFI，宏开启才轮询测试邮箱 |
| `Core/Inc/p1a_safety.h` | 将已有 raw 关断明确为内部冷启动/正常 Disable 原语；致命入口必须锁存并不返回 |
| `App/p1a_test/p1a_test.h/.c` | 新增默认关闭、逐条提交的调试邮箱，无自动扫动、无通信/RTOS；每次 Enable 消耗独立 key；可选 action=6 有限轮询周期边界窗口 |
| `doc/P1/C2_static_check.py` / `C2_static_evidence.txt` | 文本检查、寄存器语义模型及真实运行输出/文件哈希；不执行 C 或硬件 |
| `doc/P1/C2_hardware_guide.md` | H01–H10、固件选择、显式授权、原始证据与恢复默认说明 |
| `doc/P1/C2_source.diff` / 本交接 | 相对 C1 的完整源增量与审查结论 |

旧参考 SHA 为 `92d8f45bc9c7df55f6e2abea4b4abfeb090a3e3d`；同时读取该提交的 `Visionarm_MCU/doc/timer_pwm_design.md` 与 `safety_state_machine.md`。没有迁移 Gimbal/旧 UART/完整应用。Startup、system、HAL/CMSIS、FreeRTOS、`Core/Src/interrupts.c`、uvprojx、uvoptx 均未被 C2 修改。没有实现 P1b。

ABI 说明：六 API 名字与参数/返回类型保持；Snapshot 新字段使结构布局变化，所有调用者需一起重编译，不能沿用旧二进制布局。fault_count 为每次复位后的故障 episode（0/1）；其它计数为正常上下文下自然回绕 uint32。初始化历史不清零以免擦除故障。applied 是最后接受的 preload 目标，不是已输出/机械位置；普通 Disable 重置目标中位，紧急关闭保留目标供诊断，但快照 enabled 强制 false。

## 寄存器和 HAL 审查

| 检查 | 实现/边界 |
|---|---|
| AFIO/SWJ | 本库 `stm32f1xx_hal_gpio_ex.h:841` 的 AFIO_REMAP_PARTIAL 会 OR SWJ_CFG=111；不使用该宏，也不依赖 111 的效果。新代码丢弃 SWJ 未定义读回，显式写 `000`（JTAG+SWD），只替换 TIM3_REMAP=11，保留其它 MAPR 位；只读 TIM3_REMAP 验证，不能读 SWJ 证明成功。P1a 无其它 MAPR 写者；板上仍需确认调试链路 |
| GPIO | 仅 PC6/PC7 CRL 高两 nibble：禁止=0x22（GP PP 2 MHz），使能=0xAA（AF PP 2 MHz）；ODR 先低，低 24 位保留。不写 GPIOA/PA6/PA7 或 PC8/PC9。full remap 也映射 CH3/4，但这两路保持禁用且不配置引脚 |
| 时钟/配置 | 核对 HSE/PLL ready、PLL HSE/div1/x9、SWS、HCLK/APB 分频和软件频率，拒绝不同树；TIM3CLK=2×36 MHz，PSC=71、ARR=19999。读回不等于频率实测 |
| HAL 副作用 | 本库 HAL_TIM_PWM_Init→TIM_Base_SetConfig 写 CR1/ARR/PSC 并 UG；ConfigChannel 禁通道、写 CCR/极性/模式，再开启 OC preload；PWM_Start 按单通道 CCER 后启动 CEN；Stop 的通道/计数器语义依赖剩余通道。新硬件路径不用这些函数/句柄/MSP，避免回调和分通道启动；寄存器受控实现同一 PWM1/ARPE/OCPE 子集，无 HAL_TIM/HAL_GPIO .c 链接需求 |
| 初始化 | raw 关闭→校验时钟→AFIO→TIM3 外设复位→已知基线；CCER/CEN/DIER=0、SMCR=0、CCMR2=0，双中位 UG 后仍为 GP 低；任何检查失败紧急关闭锁存 |
| 首次使能 | 在 GP 低且 CEN/CCER 关闭时双 CCR=0→UG，双 CCR 中位只进 preload→同时开通道→AF 低→CEN；先完整低帧，首次自然更新再输出完整双中位。UIF 前的 Apply 拒绝并锁存，确保首中位不能被抢先覆盖。此为源码/手册推导，未测瞬态 |
| 正常 Apply | PWM1+OC1PE/OC2PE+ARPE；PRIMASK 内 `UDIS=1→CCR1→CCR2→UDIS=0`，无 UG，无停表。若跨自然溢出，则两路共同多保留一个旧帧，下次一起装载；新旧混合帧不被接受 |
| 禁止/故障 | raw 先 CC1E/CC2E 清零，再 CEN 清零，再预置 PC6/PC7 低并改 GP PP；紧急 wrapper 随后只写 sticky word。无 Tick/锁/RTOS/日志等待；普通 Disable 不清 latch，同时丢弃旧目标/preload |

依据：[ST RM0008](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) 的 AFIO_MAPR、TIM2–TIM5 CR1/CCMR/PWM 章节，及本仓库实际 HAL/CMSIS。在线读取整本 PDF 受工具大小限制，已通过 ST 官方索引段落核对 SWJ、UDIS 和 PWM/OC preload 规则；没有声称完整 PDF 已下载。剩余硬件验收不能由文档推导替代。

## 失败路径逐项审查

| 入口/触发 | 行为 |
|---|---|
| C1 HAL_Init/InitTick/时钟错误、NMI/HardFault 等 | P1a_FatalError→硬件关断→锁存→屏蔽中断并反复关闭，不返回，不恢复旧写序列 |
| InitSafe 时钟、remap 或寄存器检查失败 | false；紧急关闭锁存；不标记首次初始化成功 |
| 故障后的 Init/Enable/Apply | false，重复关闭；无清锁存/恢复路径 |
| 禁止态重复 Init | 复核当前配置后 true；不擦计数、不输出；坏配置则关闭锁存 |
| 已使能时重复 Init/Enable、未初始化 Enable | 视为调用误用，false 并故障关闭；不隐式重启/中位跳变 |
| Apply(NULL)、未使能/未初始化 Apply、首次中位装载前 Apply | false 并故障关闭；结果 applied=0（无接受请求），不增加成功计数 |
| Apply 负值/超范围 | 独立夹紧后成功，报告 saturated，正常使用不作为故障；不先把 int32 转无符号 |
| Init/Enable/Apply 寄存器读回异常 | false 并故障关闭；不能通过返回 true 掩盖失败 |
| 普通 Disable、重复 Disable | 立即 raw 关闭；只在从正常 enabled 转出时计数；不清 latch，旧目标失效；下次新授权 Enable 仍先中位 |
| 正常运动 API 被 ISR 调用 | 拒绝并故障关闭；只有紧急入口允许 ISR；Snapshot 约定正常上下文使用 |
| GetSnapshot(NULL) / Apply result=NULL | 前者无操作，后者合法省略结果；其它指针须有效，失效指针若触发 fault 进入不返回关闭路径 |
| 测试 key 错误/动作未知 | 普通 Disable，status=3；不使能、不清已有故障；key 每条命令均消耗，不复用 |
| 测试 action=6 始终未遇窗口 | 每次 Poll 一次、最多 65536 次；用尽预算则紧急锁存并拒绝 Apply，status=2；不依赖 Tick，不是时间测量 |

并发边界：正常 API 仅主循环一个所有者，保存/恢复原 PRIMASK。临界区无等待/循环/硬件超时，阻止可屏蔽 IRQ 插入 CCR/使能序列；紧急 IRQ 可能延后至短临界区结束，延迟没有实测或时间上界证明。NMI/同步 fault 不受 PRIMASK 屏蔽，当前 C1 所有相关 handler 都必须走不返回 Fatal；这样被打断的 CR1/CCER/GPIO 写不会恢复。**不得新增“调用紧急 API 后返回”的 NMI/fault handler**；若将来改变此条件或增加 RTOS/其它 TIM3 写者，必须重审，volatile 本身不能保证安全。

Snapshot 只复制普通状态并从独立 latch 合成 fault/enabled/fault_count，PRIMASK 保护普通紧急 IRQ，NMI/fault 不返回；没有让诊断复制延迟硬件关闭。关闭发生在高电平中会主动截短尾脉冲，这是即时关闭策略，不等待周期结束。

## 同一 Keil 工程的用户输入清单

工程仍为 `F:\嵌入式Linux学习资料\RT-Node\Projects\RTnode.uvprojx`，单一 target `Target 1`；文件哈希与 C1 相同：`0D843C27AB597F7053BCDA55BF91B9241AD8CB05BFD2877AC73766FA3D87060E`。uvoptx 同样未改。

实际 XML 仍为 uAC6=1、无源条目/宏/include，HEX 未启用、MAP 已启用；这些是 C1 已记录且用户决定忽略继续的差异，本轮没有据此改造工程或换工具链。**C2 没有证明当前 XML 可以构建**。用户在该工程内落实：

1. C1 基础源码：main.c、interrupts.c、唯一 `Startup/startup_stm32f103xe.s`、唯一 CMSIS `system_stm32f1xx.c`，HAL `stm32f1xx_hal.c`、`stm32f1xx_hal_rcc.c`、`stm32f1xx_hal_cortex.c`。
2. C2 增量源码 `..\App\actuator\src\actuator.c`；受控测试固件另选 `..\App\p1a_test\p1a_test.c`（默认固件可排除）。
3. 宏 `STM32F103xE`、`USE_HAL_DRIVER`，HSE_VALUE=8000000；默认不定义测试宏或定义为 0。受控固件为全局 `P1A_ACTUATOR_TEST_ENABLE=1`。
4. C1 include（相对 Projects）：`..\Core\Inc`、`..\Driver\CMSIS\Include`、`..\Driver\CMSIS\Device\ST\STM32F1xx\Include`、`..\Driver\STM32F1xx_HAL_Driver\Inc`；新增 `..\App\actuator\include`。test 源同目录包含自己的头，main 用显式相对路径包含 test 配置。
5. 不选 FreeRTOS 内核/port/heap、freertos_static、HAL timebase 模板、旧协议、额外 startup/system 或文档目录。若实际工程另有必要输入，由真实链接依赖说明，不能全库混加。

设计基准为 ARM Compiler 5.06u7。新 C 源使用块首声明/普通 `{0}`，未使用复合字面量或 for 声明；**ARMCC5 兼容性未编译确认**。实际工具链版本由用户日志记载。

本轮实际构建命令：**无（按用户指令不编译）**。供用户执行的未运行模板：

```powershell
& '<实际 UV4.exe 路径>' -r 'F:\嵌入式Linux学习资料\RT-Node\Projects\RTnode.uvprojx' -t 'Target 1' -o '<实际完整 Build Log 路径>'
```

Build Log/MAP/AXF/HEX/ROM/RAM：均无本轮证据；不填写估算值，不把历史产物当本轮结果。HEAD 不包含未提交改动，记录固件时必须同时保存 C1/C2 diff 和实际输入哈希。

## 实际验证、限制与 Gate

| 项目 | 当前证据 |
|---|---|
| S01 | 静态：HEAD/status、固定来源、无旧库修改/无推送；C2_source.diff |
| S02 | 只读 XML/哈希已核对并保留；实际构建输入/唯一链接目标仍待用户落实 |
| S03 | NOT_RUN：用户明确无需编译；无日志/MAP/资源统计 |
| S04 | 静态：执行器无旧 GPIOA/PA6/PA7/旧通信/RTOS 依赖，interrupts 保持 C1 版本 |
| S05 | 源码失败矩阵/锁存/恢复/临界区审查完成；未执行 C 分支故障注入或 MCU 测试 |
| S06 | 静态：MAPR/SWJ、GPIO 掩码、时钟和 PWM 配置核对；实际映射/调试功能待 H02 |
| S07 | 文本核对 + 独立 UEV 模型 32 种溢出交错；非真实定时器模拟或 C 执行，待 H07 |
| H01–H10 | 全部 NOT_RUN；操作见 C2_hardware_guide.md；先无负载后机械 |

实际运行：`python doc/P1/C2_static_check.py`；结果见 `C2_static_evidence.txt`，另执行 `git diff --check` 和只检查不应用的 `git apply --reverse --check doc/P1/C2_source.diff`，均通过。增量补丁基线由 C1_source.diff 还原后，与 C1 实际 main/安全头哈希核对一致。结束时旧仓库 status 与开始相同。静态/模型通过不能写成构建或板上 PASS。

剩余限制：软件首次动作前 PC6/PC7 可能高阻；软件关闭不能保证总线/电源/CPU 严重故障时执行，也不代表舵机机械停止/抱闸；运行中临界区时长、启动/禁用瞬态、真实频率、同步和机械安全均未测。默认镜像没有上电使能入口，但固件编译宏、选源和错刷风险仍需用户核对。

下一步由用户完成真实构建以及 H01–H10 并提交原始证据，再由阶段负责人判 P1a。当前不允许据此判 ACCEPTED 或启动 P1b。
