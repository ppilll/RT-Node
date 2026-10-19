# RT-Node F103 — P7 完整测试方案 V1.0

**日期：** 2026-10-19  
**代码基线：** `ppilll/RT-Node`，`main`，`c3e9408013ae009e2b6cb49ac8d53f79d169e430`（2026-10-18 P6）  
**状态：** 测试设计获原则同意；未修改仓库、未实现新增插桩、未执行本方案的实机测试。Stage1 / Stage2 不因同意 Stage0 性能测试而自动获得授权。

## 0. 目标、范围和真实功能边界

P7 交付以嵌入式软件岗位的可信简历成果为目标，工作包括：**G：ICM20602 姿态交互证据**（低成本功能验证）、**T1：多外设并发实时性**、**T2：CAN/UART 通信完整性**、**R：复用 P6 故障证据**、**M：构建资源占用**。只有 T1/T2 是新增长时间核心实验；G 优先复用现有纯 C 主机测试，Stage1 无负载波形验证属单独安全门禁。

现有 `ImuTask` 通过 ICM20602 SPI1 每10ms采样，静止标定100个连续有效样本，互补滤波融合陀螺仪与加速度计输出 Roll/Pitch。`LocalFollow_Start` 保存授权时姿态为零位；`LocalFollow_Target` 将 Roll→Pan、Pitch→Tilt，按 2° 死区、±15°限角、3µs/°、1500±45µs、每 ControlTask 周期最大2µs变化生成双轴 PWM 脉宽目标。**这是姿态映射/本地跟随，不是空间轨迹规划、手指识别、绝对Yaw或机械位置闭环。**

**关键实现事实：** `Motion()` 整段在 `#if P3B_OUTPUT_STAGE > 0` 内。正式 Stage0 不生成输出，也不执行实际 `LocalFollow_Target` 路径；仅 Stage0 实测不能宣称“真实手势驱动云台”。本次 G 主机重放证明算法，另经独立批准的 Stage1（舵机供电及信号均物理断开）用 LA 证明 PWM 响应；实际云台机械运动必须有另外批准的 Stage2。

**文档冲突纠正：** 当前 `control.c` 中 KEY0(PE4) 是停止并锁存，`display_app.c` 中 KEY1(PE3) 是 OLED 翻页。旧 P2b/P3b 测试指南要求通过 KEY0 授权或 KEY1 停机的部分已不适用，严禁照做。最新本地模式可由 CAN `MODE(LOCAL)` 进入，或者在 Flash 政策确为 `AUTO` 且本次自动本地许可时上电进入；`MANUAL` 必须获得新的有效模式请求；`LOCK/EMPTY/BAD` 不允许正常控制授权。

## 1. G0 门禁：人员、设备、版本、安全

- **人员职责：** Codex 只在授权后实现必要插桩、测试脚本与 Keil 构建；操作者确认接线、烧录、供电、CAN/UART、LA 和实际动作。所有硬件证据来自用户回执，不能由编译日志替代。
- **设备：** STM32F103ZET6、ICM20602、RK3588 SocketCAN 500kbps、PC CH340 115200/8N1、逻辑分析仪、Keil5/ARMCC5、OLED、共地以及现有安全断电方式。无需 SWD/J-Link/示波器。
- **版本记录：** Git 提交、测试固件对应的编译宏、正式 Keil Rebuild 日志、HEX、MAP；有 Stage0 和 Stage1 时必须按不同文件名区分，禁止刷错固件。
- **默认安全：** 正式性能和通信实验一律 `P3B_OUTPUT_STAGE=0`、`P6_WDG_TEST_ENABLE=0`、全部历史输出自测宏=0；舵机独立电源与信号断开。不得绕过 P6 授权锁、KEY0 锁存、运行期控制判定、现有门限或 Flash 安全页（基址 `0x0807F800`）。
- **测试状态快照：** 上电、每组测试前后通过 UART `HEALTH?` 记录 C/W/R/L/P/A/T；记录 CAN `0x280` STATUS、OLED 状态。若 `W=STARTERR`、`C=STALL` 或意外重启，停止继续施加负载，保存证据。
- **Stage1 额外门禁：** 测前口头/书面确认独立批准，舵机供电和**两根信号线**均断开；逻辑分析仪只接 PC6/PC7/GND；核对 `P3B_OUTPUT_STAGE=1` 及 P6 健康策略；不得假设上电一定无PWM——`AUTO` 策略可能允许自动进入 LOCAL。Stage2 真实带载不在本方案默认授权内。

## 2. 最小实现任务与统一统计口径

### 2.1 ControlTask (只读观测，不变更安全判断)

当前 `ControlDiagnostics` 有 `wake_count`、`late_count`、`last_period_ticks`、`worst_execution_cycles`、栈最低剩余字数。需要增加**P7 独立统计窗**，建议在开机约10s以后自动开始（明确记录开始条件；不得因工况改变该条件），字段：

1. `period_sample_count`：连续任务入口的有效 DWT 周期间隔样本数；
2. `min_period_cycles`、`max_period_cycles`：各以 `uint32_t` 对相邻 `DWT->CYCCNT` 读数作无符号回绕差；
3. `worst_elapsed_cycles`：该统计窗内既有计时定义对应的最大任务段经过周期数；
4. `period_violation_count`：相邻入口实际时间不在建议19..21ms范围的次数，**独立于原 `late_count`，即使 Control 已终态停机仍继续计数**；
5. 复用现有 `min_control_stack_words`、`min_idle_stack_words`，必要时只读 HealthTask 的 `min_stack_words`。

`Δt(µs)=ΔCYCCNT/72`，72MHz下名义20ms=1,440,000 cycles；相邻差避免32位计数器约59.65秒回绕的歧义。旧 `late_count`、故障逻辑、2,000µs执行门限、20ms任务周期**绝不修改**。`worst_elapsed_cycles` 为入口至记录点的经过时间，可能包含中断抢占和栈高水位检查，不要宣称纯CPU占用或理论WCET。

### 2.2 CAN 与 UART

- CAN 复用 `CanApp_GetDisplaySnapshot().rx_count`（CanTask 已出队处理帧计数，包含后来判无效者）；补一个任务可读的独立队列投递失败/接收溢出**事件累计**字段即可，保持已有安全使用的 `s_rx_overflow` 事件标志原语义，不通过读取清除事件；不改变 `0x180..0x183` 线上格式。
- UART 复用 `COUNT?` 的 `RX` 与 `OVF`；`RX` 表示接收进度字节数，`OVF` 是检测到的错误/溢出事件，不是精确丢失字节数。`PING/PONG` 正确率只能由主机解析统计。
- 由**原 UartTask 单一发送者**新增只读查询 `PERF?` 和（需要时）`CANC?`。每条输出长度必须经最长32位十进制值测试仍适配当前64B回复缓冲；保持每次查询低频（例如5s），保留 PING、COUNT?、HEALTH?、ADC?、ENV? 和 SAFE 命令现有行为。不得每20ms打印。
- `PERF?` 不提供运行期清锁、重置安全状态、暂停调度或在高优先级中输出串口功能；如需统计窗口的重置，优先通过每组独立重启及固定开机10s预热实现，而非新增具有副作用的命令。

### 2.3 脚本

PC 串口脚本：发送计划、事务ID/时间戳、PONG/ADC/其他行分类、COUNT?/PERF?/HEALTH? 聚合、异常和原始CSV/JSON。RK3588 SocketCAN脚本：只发送既有合法 `0x183` 心跳帧（版本/计数字段按源码），速率控制使用单调时钟；记录计划数、系统实际 `send()` 成功数、异常、接口统计；CAN接收另记来自 STM32 的 `0x280` STATUS，不把 Linux 本地 loopback 当对端收包。脚本不可自动写 Flash 或将异常后重试计作新的唯一消息。原则上只需两个主机脚本与小的 CSV 汇总脚本。

## 3. G：ICM20602 手势/姿态跟随证据

### G-A：纯C确定性测试（**必做**，不烧录）

使用现有 `doc/P2b/tests/local_test.c` / `run_host_tests.py`（文件名与当前目录以复核为准），并复用 `ImuFusion` 测试：

| ID | 输入/工况 | 期待结果 | 证据 |
|---|---|---|---|
| G-A1 | 静止有效重力和低角速度100连续样本；期间运动打断 | 达到READY后发布稳定Roll/Pitch；不合格窗口不通过 | 主机测试输出，IMU状态 |
| G-A2 | 授权时任意近水平姿态为零位 | 初始目标1500/1500µs | 单元断言 |
| G-A3 | Roll +1.9° / ±2°与 +5°、Pitch独立变化 | ±2°以内零偏移；+5°对应稳定目标+15µs（输出需经逐步限速） | 输入/目标/每步输出 |
| G-A4 | 任意轴±20°（超过±15°） | 对应目标最终夹在1455..1545µs | 上下限断言 |
| G-A5 | 由1500跃迁到1530 | 每次`LocalFollow_Target`更新至多2µs，逐步收敛 | 完整步序 |
| G-A6 | NaN、Inf、过期`sample_tick`≥40ticks、失去READY、重复generation | 拒绝错误命令或按现有控制故障路径保护，不以无效数据驱动目标 | 原控制策略主机回归 |
| G-A7 | 从LOCAL到REMOTE、REMOTE到LOCAL、STOP | 两种源互斥，新epoch/授权生效，旧目标不可继续输出 | 主机策略回归 |

注意：G-A3中的+15µs为**稳定目标**，第一周期从1500开始只能变化至多2µs；死区中也不是瞬时回中。没有独立实测输入角度的精密参考设备，不要求计算机械角度跟踪误差。

### G-B：真实板卡的 IMU 输入（**必做**，Stage0允许）

在Stage0上电后保持ICM20602静止约2秒，OLED切至IMU页，确认Roll/Pitch有连续更新、姿态正反倾斜变化方向合理；静止记录30秒，固定夹具或水平参考支架下记录偏移、最大变化；再缓慢分别倾斜Roll、Pitch各3轮，每轮改变方向再回中。录制简短视频或屏幕和条件说明；不凭肉眼读OLED声称0.1°精度或定量延迟。若需精确数值，应另加只读低频 `IMU?`，但非P7最低要求。

### G-C：真实双轴PWM响应（**条件性，Stage1另行批准**）

舵机供电和信号完全断开，逻辑分析仪≥1MHz采样（建议2MHz以上）接PC6/PC7/GND。`HEALTH?` 先确认 `P` 与 `A`；`MANUAL` 仅在新的有效 CAN MODE(LOCAL) 后授权；`AUTO` 可能上电自动进入本地；`LOCK/EMPTY/BAD` 不允许人为绕过以测试。使用现有 `doc/P3b/rk3588_test.py mode local` 只作新模式请求（**不用旧 `--end local` 脚本显示的“按 KEY0 启动”操作**），需要时按最新状态获得有效epoch/action。

- G-C1：在Disabled时 PC6/PC7 LOW；成功授权后先中心脉宽、≥40ms PRIMING、约50Hz双路输出，记录LA时序；若AUTO上电授权则记录为AUTO工况而非“初始必不输出”。
- G-C2：手动缓慢滚转±5°、±10°、前后俯仰±5°、±10°，各3轮；验证Roll仅主要改变PC6、Pitch仅主要改变PC7、回中趋势、方向、波形不串轴。无精确角度夹具则只报告“方向/趋势正确”，不声称目标脉宽与真实角度的严格误差。
- G-C3：短窗口测量输出脉冲宽度限幅1455..1545µs（本地映射配置下）、相邻有效目标变化不大于2µs/控制周期；检查20ms PWM周期（建议单帧周期在19.8~20.2ms的LA容差以内，前提是仪器采样率正确）。
- G-C4：运行中通过CAN `0x182` 全局STOP或按KEY0停止，确认PC6/PC7回LOW、旧目标不会重放；KEY1仅翻页，不应作为STOP；若出现终态FAULT，仅记录不反复授权。

Stage1 完成后如需简历强调“**真实云台机械运动**”，必须在独立批准的Stage2完成一次小幅带载测试，并另存机械观察、独立供电和停机证据；未批准时仅写“姿态映射生成PWM控制目标，并完成无负载波形验证”。

## 4. T1：多外设并发实时性（必做）

### 工况与顺序

**两组都使用同一个正式Stage0镜像、相同10s启动预热、相同查询周期。** 最好每组独立安全重启，重启前记录当前Health；重启只为重置性能统计，不擦除Flash安全页。主机使用单调时钟记录测试开始/结束；若异常重启本组判FAIL并记重启时刻。

- **T1-A 基准组：2分钟。** 保留ControlTask 20ms、ImuTask 10ms、ADC TIM2+DMA、SensorTask、DisplayTask、HealthTask；无持续外部CAN注入，UART仅每5秒一次简短性能查询；允许既有200ms ADC自动报告。
- **T1-B 并发组：10分钟。** RK3588 向`0x183`发送合法心跳帧**目标50帧/秒**（间隔20ms）；PC向USART1发送`PING`**目标10条/秒**（每100ms）；其余任务不关闭；每5秒读取性能、CAN处理计数、串口统计，开始/结束各读`HEALTH?`。所有主机实际发送数和异常单独记录，不把设定速率冒充达到速率。

### T1指标与计算

| 指标 | 单位/算法 | 推荐判据 |
|---|---|---|
| 相邻ControlTask入口周期min/max | DWT cycle÷72→µs | 建议19,000..21,000µs；不覆盖原Tick故障判定 |
| 最大绝对周期偏差 | `max(|min-20000|,|max-20000|)` µs | 作为实测数据报告 |
| 窗口内周期违例次数 | 相邻入口超19..21ms的独立计数 | 建议0；非0照实记FAIL/边界 |
| 最大观测执行段耗时 | DWT cycle÷72 µs | <2,000µs（与现有源码阈值保持一致） |
| Control/Idle/Health栈最低剩余 | FreeRTOS stack words，必要时×4B | 必须>0；具体最小值如实记录 |
| 异常故障/IWDG复位 | 控制故障模式、`HEALTH?`、主机uptime重置 | 0意外重启，0未预期终态故障 |

本测试只能叫“观测最坏值”而不是数学意义 WCET 证明。周期偏差、任务唤醒延迟、任务执行时间、PWM硬件周期分开统计，Stage0 不证明 PWM 物理输出。FreeRTOS高水位不是瞬时栈使用量；统计函数本身有开销，原已在ControlTask中每周期执行，保持A/B一致。

若T1-B失败，首先保存原始异常和负载。可进行**一轮**具有明确原因的优化，保持同条件重测并单列新旧版本；不能为了简历改判据。

## 5. T2：CAN/UART完整性与共存（必做，与T1-B复用长测）

### CAN：T2-C

1. RK3588运行 `ip -details -statistics link show can0`，核对 `500000` bit/s、Classical CAN、总线正常。若需要重新配置，先确认没有其他应用占用：`sudo ip link set can0 down`，`sudo ip link set can0 type can bitrate 500000 fd off`，`sudo ip link set can0 up`。保留前后接口错误统计。
2. 单一发送脚本按照源码现有 `0x183` 8字节HeartBeat格式发送合法数据，计数器递增。正常目标50帧/s × 600s ≈ 30000帧，**以脚本实际提交并返回成功的数量为准**。固定ID且无其他发送者，避免污染统计。不中途重新配网。
3. 测前测后通过新的`CANC?`读取CanTask `rx_count`的差值，另读取独立RX队列投递失败事件累计数；同时 `candump -L can0,280:7FF` 记录STM32发出的状态报文，仅用于佐证对端存活，不将其条数作为接收成功数。
4. 报告 `处理比例=(CanTask处理数差值)/(RK发送调用成功数)`，前提写清**单发端/合法帧/ID过滤/统计窗口对齐**。比例不能声称物理链路精确丢包率，队列事件计数也不能当精确丢失帧数。若不相等，检查队列、CAN错误、发送时刻、起止竞态与重传，不直接推断损失位置。
5. **短协议烟测（约30秒，与10分钟统计分开）**：在正式 Stage0 用现有 `rk3588_test.py mode remote` 发送合法 `0x181` 请求，按回传 `0x280` STATUS 验证新 `epoch` 和 `WAIT_REMOTE`；再发合法 `0x180`、递增 `seq` 的小幅目标，并以 `0x182` STOP 验证回到 `DISABLED`。Stage0不会实际使能PWM，也不执行 `Motion/Apply`，这里只证明接收、解析、模式仲裁与STOP的**部分软件路径**。不得拿该烟测宣称Stage1真实输出，脚本不得擅自切到带输出镜像。

### UART：T2-U

1. 串口配置115200/8N1，关闭自动重置板子的设置（若串口工具存在）。测试前后各读`COUNT?`，并保留原始行日志。
2. 10分钟内按目标10条/秒发送`PING\n`，将完整`PONG\r\n`按行准确分类；允许ADC自动报告插入，不能每行都按PONG统计。
3. 单独约30秒的短边界测试：将`PING`在多次写操作中分段发送；累计发送数据远超过256B以跨DMA Circular环；交替`PING\r\n`和`COUNT?\n`；检查正确回复数、解析未污染、OVF事件。保持速率合理，不制造持续缓冲溢出。
4. 输出`PING发送数`、`PONG正确数`、`timeout/ERR/其他行`、`RX字节增长`、`OVF事件变化`、`ControlTask同期违例/故障`、串口测试持续时间。字节增长需包含`COUNT?`及其他查询输入，不直接等于PING负载字节数。

**建议判据：** 在上述非饱和负载下PONG正确回复率100%、无新的OVF事件；CAN处理数与发送成功数在可解释的窗口对齐后相符、无新的队列溢出事件；无意外控制故障/IWDG复位。任何不合格都报告负载、原因和实际可承受范围，不粉饰。

## 6. R：P6故障管理证据复用（不新设长测）

从用户已完成的P6实机记录中提取：（R1）健康RUN与IWDG ON；（R2）受控停喂引发的MCU确实复位而非简单串口失联；（R3）重启后`R=IWDG`、`L=1`、`P=LOCK`；（R4）新的普通控制请求不能绕过锁；（R5）既有NRST/POR后的持久锁（如有实际记录）；（R6）两步本地恢复后`P=MANUAL`且`A=0`。对没有证据的项记`USER_REPORTED`或`NOT_VERIFIED`，仅在重要事实完全缺失时申请一次无输出条件下的最小补验；禁止为复测清空Flash安全页。

IWDG预分频64、Reload1249在LSI名义40kHz下约2s，LSI 30..60kHz只支持约1.33..2.67s的粗略理论范围（不是实测重启延迟）。监督停滞判断约300ms，实际检测/停喂到重启还有调度、上次喂狗和LSI不确定性；不得写“固定300ms复位”或“精确2s故障恢复”。

## 7. M：构建、内存与回归

- Keil5/ARMCC5 正式 Rebuild Stage0，记录0 error/0 warning（若出现则FAIL）、HEX/AXF/MAP与编译宏；重新统计 `Code+RO+RW` Flash、`RW+ZI` 静态RAM。P6b旧生产构建记录Code43694/RO1062/RW248/ZI16448，合计Flash45004B、静态RAM16696B，**仅作旧版本基线**，不是P7最终值。
- 记录 ControlTask、IdleTask、HealthTask 最低剩余栈（字及字节）；必要时仅记录这些关键任务，不追求全任务CPU空闲率。
- 复用现有 P2b/P3b/P6 主机测试、通信回归、Stage0/Stage1编译宏负向守卫。特别核查`Stage0`未把`Motion`真实输出路径纳入；Stage1只有独立授权后才构建/执行。
- 源码变更只应有Control独立性能统计、CAN独立只读计数/事件（如需）、UART两条短查询、PC/RK主机脚本、文档；不得改Control周期/优先级、原安全故障逻辑、CAN线上协议、UART DMA策略或IWDG条件喂狗。

## 8. 操作顺序、停点和可执行命令

| 顺序 | 操作 | 负责人 | 放行/停点 |
|---|---|---|---|
| 1 | 固定提交与安全页状态，记录`HEALTH?`、`can0`状态 | 用户 | G0通过；异常先处理 |
| 2 | Codex 最小插桩，Keil Build、主机测试、命令长度测试 | Codex/用户核验 | G1：0构建错误；无安全回归 |
| 3 | G-A纯C姿态映射单测与G-B Stage0 IMU观察 | Codex/用户 | 记录算法PASS及板载观察 |
| 4 | T1-A 2分钟基准 | 用户+脚本 | G2：数据完整、无重启 |
| 5 | T1-B + T2-C/U 10分钟共测 | 用户+脚本 | G3：主机计数与MCU诊断一致 |
| 6 | T2-U 约30秒边界/分段回绕 | 用户+脚本 | 记录PONG、RX、OVF |
| 7 | R复用P6、M更新MAP及栈高水位 | 用户+Codex | G4：证据明晰 |
| 8 | **可选**G-C Stage1无负载LA | 另行授权后用户 | 未授权为NOT_RUN，不能写PWM实测 |
| 9 | 整理两张结果表，生成简历标准版与STAR面试版 | P7技术负责人 | 可量化字段均有出处 |

示例终端操作（**现有命令**，具体COM端口取用户机器实值）：

```text
PC串口 115200 8N1：
HEALTH?
COUNT?
PING
ADC?
ENV?
# PERF? / CANC? 在Codex新增且构建验证之后才可使用
```

```bash
# RK3588：只读核查
ip -details -statistics link show can0
# 若can0未配置，需确认无其他CAN占用后再配置：
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 fd off
sudo ip link set can0 up
# 接收STM32 STATUS帧作为通信活性佐证：
candump -L can0,280:7FF | tee p7_status.log
# 原仓库已有Stage1模式请求工具；仅已批准的Stage1使用：
python3 doc/P3b/rk3588_test.py status
python3 doc/P3b/rk3588_test.py mode local
```

注意：`rk3588_test.py` 内`remote --end local`的旧“按KEY0启动”提示**与当前KEY0=STOP源码不符**，未经更新不要以该提示执行动作。

## 9. 最终原始数据与验收表

**每条样本元信息：** `test_id`、固件git提交和build宏、Flash持久状态、Stage、测试起止时间（PC与RK3588时钟各自单调；无需虚构跨机同步精度）、测试持续秒数、CAN/USART配置、实际输入速率、自动ADC报告开关状态、测试人员、原始日志文件名、结果PASS/FAIL/NOT_RUN及异常备注。

| 指标 | T1-A基准 | T1-B并发 | 证据 |
|---|---|---|---|
| period_samples | 待测 | 待测 | PERF查询 |
| period_min_us | 待测 | 待测 | DWT |
| period_max_us | 待测 | 待测 | DWT |
| max_abs_deviation_us | 待测 | 待测 | 计算 |
| worst_elapsed_us | 待测 | 待测 | DWT |
| period_violation_count | 待测 | 待测 | 独立统计 |
| control_stack_min_words | 待测 | 待测 | HWM |
| health_stack_min_words | 待测 | 待测 | HWM |
| unplanned_reset / control_fault | 待测 | 待测 | HEALTH/STATUS |

| 通信项 | 实测数据 | 注释 |
|---|---|---|
| RK CAN send()成功数、实际频率 | 待测 | 不等于STM32接收数 |
| STM32 CAN ProcessFrame增量 | 待测 | 包含后续判为无效帧 |
| CAN RX队列投递失败事件 | 待测 | 不是精确线损数 |
| UART PING发送/PONG正确/超时 | 待测 | 区分ADC自动报告 |
| UART RX字节与OVF事件增量 | 待测 | 不声称精确丢失字节 |
| 该窗口Control违例 | 待测 | 用T1-B数据 |

| 手势/姿态项 | 实测结论 | 可说到什么程度 |
|---|---|---|
| G-A纯C映射、限幅、限速测试 | 待执行 | 算法验证 |
| G-B板卡IMU读数与翻转 | 待执行 | 实机姿态输入 |
| G-C无负载PWM波形 | 待授权 | 电气输出验证 |
| Stage2带载云台运动 | 未授权 | 不宣称真实机械轨迹 |

**P7 验收：** T1/T2工况与数据可复现、Stage0正式固件回归通过、P6已测事实有回执、资源统计更新、手势映射证据与Stage限制明确、3～4条简历陈述无虚构数字、3个STAR故事有测量依据。若G-C未授权，可完成Stage0 P7验收，但简历不得写成“实际云台轨迹已通过带载精度验证”。

## 10. Codex任务边界（待实施）

- C1：检索主机现有P2b单测与P3b脚本，新增/补齐 G-A 断言，纠正旧KEY0/KEY1指南冲突，保留源代码安全机制。
- C2：ControlTask增加启动预热后只读统计窗；测试DWT回绕、截止期故障后统计不断、并发读取一致性、长计数溢出行为。
- C3：CAN处理数/队列异常独立可读快照，UART `PERF?`/`CANC?` 分配有限缓冲，确保最大uint32输出不越界；无阻塞ControlTask记录。
- C4：PC串口与RK SocketCAN单调时钟脚本、CSV/JSON聚合；回归 ADC报告交错、行分段、环绕和错误分类。
- C5：Keil正式 Stage0 Rebuild、MAP、新旧主机测试；不得自动下载或切Stage1/2；编写一页人工操作说明与结果模板。

## 11. 写简历的规则

可写候选：“基于ICM20602与10ms IMU采样任务实现Roll/Pitch融合及相对姿态控制，设计2°死区、±15°映射、2µs/20ms脉宽限速与输入有效期保护；经【算法主机回归/Stage1 LA测量】验证双轴控制目标与输出行为。”括号必须替换为真正完成的证据状态。

T1和T2只填写真实并发结果；P6只填写有可追溯硬件证据的锁定结果；不把CAN 500kbps、UART115200或IWDG名义2s配置当测得吞吐/复位时长；没有旧版本同负载对照，不写提升XX%。

## 12. 参考材料

**仓库源码：** https://github.com/ppilll/RT-Node （特别是 `App/control/local_follow.c`、`p2b_config.h`、`control.c`、`App/imu/src/imu_fusion.c`、`App/health/*`、`doc/P3b/rk3588_test.py`）。

**官方资料：** TDK ICM20602产品页 https://product.tdk.com/en/search/sensor/mortion-inertial/imu/info?part_no=ICM-20602 ；ST STM32F103ZE产品页 https://www.st.com.cn/content/st_com/zh/products/microcontrollers-microprocessors/stm32-32-bit-arm-cortex-mcus/stm32-mainstream-mcus/stm32f1-series/stm32f103/stm32f103ze.html ；FreeRTOS `xTaskDelayUntil`文档 https://github.com/FreeRTOS/FreeRTOS-Website-Content/blob/main/content/en-us/Documentation/02-Kernel/04-API-references/02-Task-control/03-xTaskDelayUntil.md ；Linux SocketCAN https://docs.kernel.org/networking/can.html ；ARM Cortex-M3 TRM https://documentation-service.arm.com/static/5e8e107f88295d1e18d34714 。
