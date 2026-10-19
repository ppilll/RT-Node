# 给 Codex App 的启动提示词（完整复制）

你是 `ppilll/RT-Node` 的 P7 最小性能测量与手势跟随证据实现负责人。**目标是完成能够在用户硬件上运行的正式 Stage0 测试固件、PC/RK3588 测试脚本以及可复现的主机/构建验证，而不是代替用户完成硬件测试或提前写简历漂亮数字。**

## 0. 输入和批准范围

用户批准 P7 方案：`reference/P7_APPROVED_TEST_PLAN_V1.0.md`。本包 `README.md`、`source_audit.md`、`measurement_plan.md`、`implementation_plan.md`、`codex_tasks.md`、`test_plan.md`、`operator_runbook.md` 全部作为开发规范。

仓库 `https://github.com/ppilll/RT-Node`。2026-10-19 核查到 main HEAD 为 `c3e9408013ae009e2b6cb49ac8d53f79d169e430`（P6）。**你必须在开始时读取当前 checkout 的实际 HEAD/工作区差异和源代码**，不要凭旧文档推断当前事实；如果 HEAD 不同，先列出具体差异与安全影响，再在不破坏当前代码的条件下工作。不允许为了匹配旧提交做强制reset。不要自行向远端推送/建立PR。

## 1. 绝对不可更改

- 不改 ControlTask 20ms、原 `late_count`/终态故障语义、执行超时2,000us、优先级、PWM/Actuator 逻辑、IWDG 300tick监督/喂狗策略。
- 不绕过 P6 Flash 持久安全锁、控制授权/KEY0锁存；不擦除/重建地址 `0x0807F800` 的安全页，不更改它的记录格式与布局。不添加串口/RTOS命令执行 SAFE CLEAR 或远程启动。
- 正式固件固定 `P3B_OUTPUT_STAGE=0`、`P6_WDG_TEST_ENABLE=0`、旧输出自测全关。不得擅自下载到板卡、切Stage1/Stage2、接通舵机或执行真实故障注入。
- 不变更CAN `0x180..0x183` 等现有线上协议、现有USART1 DMA Circular接收架构、已有串口命令语义；不得在20ms ControlTask中调用阻塞日志/UART/printf；不得新建通用 tracing 框架、动态堆分配或多余任务。
- 旧 P2b/P3b 文档“KEY0启动/KEY1停止”**在当前P5/P6源码已过时**：现为 KEY0(PE4)=STOP锁存，KEY1(PE3)=OLED翻页。重新查代码并修正文档/脚本的危险提示。

## 2. 需要实现（按 C1～C5）

- **C1 G-A 主机回归**：复用 `doc/P2b/tests/local_test.c`、IMU fusion 与既有测试入口，检查零位、2°死区、±15°限角、3us/°比例、1500±45us、每次2us限速、NaN/Inf、40ms样本TTL、generation/授权/切换；只补缺项，不重写算法。Stage0不会编译实际 `Motion()` PWM路径，不能标成板上手势输出通过。
- **C2 ControlTask 观测**：保留旧 `ControlDiagnostics` 及所有安全判断，在开机10秒后建立P7独立累计统计：相邻任务入口DWT周期样本数、最小/最大cycles、入口周期超过19..21ms次数（即便旧控制session已terminal仍持续统计）、该窗口最大“入口至既有测量点”elapsed cycles。用`uint32_t`相邻差处理DWT回绕；0样本、计数溢出、故障后继续观测、并发读取必须经主机测试。记录统计窗口起点/有效样本数，不把elapsed称为纯CPU WCET。
- **C3 UART/CAN只读查询**：复用 `CanApp_GetDisplaySnapshot().rx_count`（出队处理总数，包含格式被拒帧），给CAN队列投递失败及必要的RX溢出**事件**增加最少独立累计字段，但绝不读取或清除已有 `s_rx_overflow` 安全标志；提供 `PERF?`、`CANC?` 及必要的第二行诊断查询（例如 `PERF2?`），只读，低频。每条USART1回复在全部数值都是uint32最大十进制时仍不超过原64B缓冲，必须新增最大值格式/边界单测；唯一TX仍属于UartTask。不要使用STM32 `printf`或新动态分配。
- **C4 可运行脚本**：`doc/P7/scripts/p7_uart.py`（pyserial，115200/8N1、串行请求+行分类、ADC自动报告可交错、PING10/s、5秒PERF/CANC/HEALTH采样、CSV/JSON结果、人工可配端口、异常保留）；`doc/P7/scripts/p7_can.py`（Python3 SocketCAN标准库、`0x183`合法heartbeat、50帧/s单调时钟、不抢发、记录计划/成功/失败/总线状态、不自动修改can0、可作为独立发送器；观察0x280但不把本地loopback当MCU接收）；必要时轻量数据汇总。脚本必须具备 `--help`、参数校验、模拟/无设备时可运行的解析测试，不自动烧录、复位、解锁或开始控制运动。
- **C5 真实回归与交付**：复用现有 P2b/P3b/P6 测试入口，跑可运行主机测试；如果机器有 Keil5/ARMCC5 完成**正式Stage0 Rebuild**并记录MAP `Code+RO+RW` / `RW+ZI`、编译器及宏、hex地址不侵占安全页。如果工具或设备不存在，应写`NOT_RUN`，绝不可报`PASS`。文档放在`doc/P7/`：`README.md`、`measurement_plan.md`、`implementation_plan.md`、`test_plan.md`、`codex_tasks.md`、`P07_HANDOFF.md`；额外结果只用简短模板，不创大型目录。

## 3. 正式测试规格（由用户后续执行，不由你实际执行）

- T1-A：正式Stage0；开机10s预热后基准运行120s，CAN不持续注入，USART每5s少量诊断；IMU10ms、Control20ms、ADC DMA、ENV/OLED、Health全部保留。
- T1-B / T2联测：同一正式Stage0镜像，启动10s预热后运行600s；RK3588向CAN `0x183`发送**目标50帧/s**，PC `PING` **目标10条/s**，每5s获取聚合统计。记录实际发送数、正确PONG、`COUNT?` RX/OVF、CanTask `rx_count`增量与队列事件、Control周期极值/违例/worst elapsed、健康状态与栈余量。工况不变情况下无意外复位/Control终态异常为目标，不可制造数据。
- T2短测：30s UART分段与>256B累计回绕；30s Stage0 CAN MODE/CONTROL/STOP“协议烟测”，不得宣称Stage0实际PWM输出。
- G-B：板卡OLED IMU输入观察；G-C Stage1无负载LA **需重新单独批准且由用户执行**，Stage2带载尚未批准；P6 R仅复用真实证据。

## 4. 具体完成顺序

先列源文件/测量接口审计与详细改动列表，随后**直接实施**C1-C5，在同一会话内完成可实现的部分；不要把这份明确的编码请求退回成“只给方案”。有问题可合理作保守决策，并写明tradeoff。严格控制文件变动，只做少量必要改动。

每一步必须区分：`PASS`（已实际运行）、`FAIL`、`NOT_RUN`（无设备/工具）、`USER_REPORTED`（只收到用户陈述），不能把构建、模拟和实机相互替代。遇到安全前提不满足，停止对应危险步骤但继续完成其他安全的代码和文档工作。

## 5. Codex 最终必须交付

1. 文件级变更清单及理由，尤其说明 `ControlTask` 计时点和额外开销。
2. `PERF?`/`PERF2?`/`CANC?` 实际输出格式与最大长度；脚本`--help`及本地模拟测试方式。
3. 主机测试结果、Keil Rebuild 记录（无Keil即NOT_RUN）以及正式Stage0编译宏核对。
4. 一套**可由用户复制执行的明确命令行**，先安全核查、再120s基准、600s联测、30s短测；不需要在线调试器。
5. `doc/P7/P07_HANDOFF.md` 明确“代码就绪/主机通过/实机未执行”的状态，真实数据保持`待测`，不得先替用户填写P07_RESULTS/P07_RESUME_EVIDENCE。
6. 列出任何尚需人工确认的风险和安全前提；停止在用户实机测试前，不擅自开始Stage1/Stage2/Flash/IWDG注入。
