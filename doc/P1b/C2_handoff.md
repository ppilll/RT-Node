# P1b C2 交接

日期：2026-10-17（Asia/Shanghai）。**C2 源码、有界无舵机镜像、真实编译/资源证据与主机测试完成；板上测试及外部时序 NOT_RUN，P1b PARTIAL。** 没有烧录、带载、Git commit、push 或自动启动P2。

## 基线、授权与范围

实际本地 HEAD 仍为 `7588842c29bfc0d1f5fce3011c727b4df38fc3d5`（P1a）。用户请求以“已提交且验证C1”为前提，但真实 C1 仍在未提交工作树，历史C1交接只有构建PASS、硬件NOT_RUN；已在执行时明确说明，不虚构C1提交或实机结果。本轮按用户明确C2请求在该保留的C1源码基础上继续源码/构建，不把它写成正式Gate已通过。P1a Case1–3沿用OPERATOR_REPORTED_PASS、不重复测试；H10仍无完成记录。

仓库/父级未发现适用AGENTS.md。已读requirements/design/interfaces/implementation_plan/test_plan与实际驱动合约；未创建新规则。用户明确答复：**本轮不新增引脚，时序实测标NOT_RUN**。未占GPIO/USART1等诊断资源，未擅选trace脚。

`evidence/C2/entry/` 保存进入时HEAD/status/diff、相关源码哈希与C1共享文件副本。`evidence/C2/C2_source.diff` 是相对本轮进入状态的C2增量，避免混入已有P1a/C1改动。没有reset/rebase/覆盖用户本地工作。

| 实际修改/新增 | 内容 |
|---|---|
| `App/control/control.h/.c` | 静态TCB/256words栈、唯一ControlTask、20tick周期、启动检查、DWT/迟到/预算保护、只读诊断；正常Enable/Apply/Disable调用仅在该任务。 |
| `App/control/control_config.h` | 默认关闭、P1a互斥、显式无舵机编译授权、case1..4、禁止未批准trace；固定时基/TTL/预算。 |
| `App/control/control_policy.h/.c` | 完整局部TEST快照、单次会话门禁、TTL/范围/生成号半环校验、拒绝generation消费、永久终止与防重放。 |
| `App/p1b_test/p1b_test.h/.c` | 同一ControlTask中的有限TEST源；中位、小幅逐轴变化与过期/迟到/执行超限注入，无其它写者/输入协议。 |
| `Core/Src/main.c` | 将C1占位任务移入control模块，原安全/时钟/InitSafe/tick handoff保留，改为Control_Init。 |
| `Core/Inc/FreeRTOSConfig.h` | 只启用Idle handle读取，以取得Idle栈余量；优先级注释更新，其它C1配置不变。 |
| `Projects/RTnode.uvprojx` | 原单一工程/单一target增加上述三个.c源文件，无第二工程，不添加kernel/heap/外设依赖。 |
| `doc/P1b/*`及`evidence/C2/*` | 实际接口补充、验证脚本、主机测试、人工步骤、补丁、日志、MAP与镜像。 |

保护文件哈希检查PASS：`App/actuator/*`、`App/p1a_test/*`、`Core/Src/interrupts.c`、`freertos_static.c`、`rtos_hooks.c`、`Projects/RTnode.uvoptx`与进入时完全一致。保留TIM3/PC6/PC7/full remap/50Hz/1µs/全部限幅/最早关断/默认禁用/故障锁存。未增加CAN/IMU/UART DMA/ADC DMA、完整状态机、远程命令、队列/软件定时器/额外任务。

## 行为与安全合约

- 默认未授权：每20 ticks运行检查与栈采样，不Enable/Apply；最终链接不包含Enable/Apply、会话授权或TEST源代码。
- 受控镜像：三个编译条件全部满足才可能单次授权。预等待>=2000 ticks；先验证完整新鲜中位快照再Enable。至少40ticks且DWT>=40ms后才允许Apply；驱动自然UEV检查仍保留。无需SWD或UART输入。
- 7个约4s有限阶段：中位、Pan1450、Pan1550、中位、Tilt1450、Tilt1550、中位，共1400个20tick活动step；约30s后正常Disable并terminal，不在本次启动中再授权。
- TTL=40、上限100 ticks，年龄>=TTL即失效；source只能TEST；generation按半环比较且拒绝新号也消费。正常完成和故障均不接受旧/新命令恢复，ControlTask不重新InitSafe、不清驱动latch。复位启动新的本地生命周期，不提供跨复位持久化防重放协议；测试镜像每次复位会重新运行，只能用于无舵机环境。
- xTaskDelayUntil不阻塞、wake晚于1tick、相邻tick间隔越出19..21、执行窗口>=144000cycles、时基检查异常、过期或驱动拒绝均硬件关断并锁存。终止后继续安全调度，不追赶旧输出。fatal/assert/overflow沿用不返回的C1路径。
- DWT仅内部辅助，不重置CYCCNT。启动检查存在/使能/增计；周期与RTOS tick作±2ms一致性检查。没有外部读出或时序PASS主张。完全停Tick/任务永久卡死/调度永久停止不由本任务独立保证，未实现watchdog。

具体接口、字段、单位和默认宏见`interfaces.md`第7节。

## 实际构建与资源

MDK5.36 / ARMCC、ARMASM、ARMLINK **5.06 update7 build960**；同一 `Projects/RTnode.uvprojx` / `Target 1`，C99，工程Optim=2（-O1）、split sections。18个编译源：C1的15个加control、control_policy、p1b_test；kernel仍只有tasks/list/RVDS ARM_CM3 port，无queue.c/heap_x.c。

执行 `./doc/P1b/C2_build.ps1`：同一工程依次构建case1..4，再按字节恢复未授权工程并Rebuild default。五次退出码均0，**全部0错误/0警告**；时间/实际宏在各目录`build.json`。脚本只编译，不下载。最终`Projects/RTnode.hex`与`evidence/C2/default/RTnode.hex`一致。

| 镜像 | Flash ROM bytes | RAM RW+ZI bytes | 结果 |
|---|---:|---:|---|
| default | 8060 | 3672 | BUILD PASS |
| case1 名义有限测试 | 9184 | 3672 | BUILD PASS |
| case2 过期注入 | 9192 | 3672 | BUILD PASS |
| case3 迟到注入 | 9204 | 3672 | BUILD PASS |
| case4 执行超限 | 9228 | 3672 | BUILD PASS |

default：Code=7600、RO-data=360、RW-data=100、ZI-data=3572。Flash含RW初值；RAM为RW+ZI。Control TCB=72bytes @0x200002CC，stack=256words/1024bytes @0x20000314；Idle TCB=72bytes @0x20000064，stack=128words/512bytes @0x200000AC；内部诊断44bytes @0x20000714。全部镜像布局相同。原Startup的MSP1024bytes/C runtime HEAP reserve512bytes保留并计入RAM；无FreeRTOS heap、malloc/calloc/realloc/free链接调用。**分配大小不等于实测栈余量**。

真实日志仍显示本机注册名`V6.16`，而HTML Tool Versions/独立`armcc --vsn`/MAP实际均为5.06u7；未为改标签修改全局Keil配置。证据保留原文。

默认HEX SHA-256：`978EF008CEFC500ACF65161A672CBD2683252980C0E735B5BF8353FEFF88BAD1`。

其它镜像完整哈希见`evidence/C2/resources.json`和每个镜像`hashes.json`。每个目录均含完整rebuild.log、build_log.htm、MAP、AXF、HEX、BIN、linker response/scatter；最终源哈希见`final_hashes.json`。

## 测试结果与证据边界

| 检查 | 真实结果 |
|---|---|
| 5个ARMCC镜像Rebuild/Link | PASS，0 errors/0 warnings |
| 5份最终二进制vector slots11/14/15及强定义 | PASS：SVC/PendSV→port，SysTick→原interrupts，非weak fallback |
| 静态Control/Idle分配、最小kernel、无动态分配 | PASS：MAP/工程/链接检查 |
| 默认镜像无Enable/Apply/授权/测试源；最终输出恢复default | PASS：最终链接与文件比对 |
| 11项ARMCC负向编译 | PASS：缺ACK/key、错误key、非法enable/case、残留生产授权、未批准trace、P1a/P1b并开均拒绝 |
| 真实C policy主机运行 | PASS：未授权、NULL、TTL0/过大/边界、未来时间戳、Tick/生成号回绕、重复/半环号、范围、终止重放 |
| 真实C ControlTask+TEST源主机运行 | PASS：15个场景，默认/名义/rollover、无DWT、创建失败、不阻塞、外部故障、Enable/Apply拒绝、过期/迟到/执行超限；保持安全终止、最多一次Enable、首Apply等待满足 |
| B01–B03板上默认输出/调度/Tick | NOT_RUN；未烧录或操作板卡，主机stub不代表真实FreeRTOS |
| B04/B05周期/执行时间、关断时延 | NOT_RUN；用户明确本轮无新增引脚，不能用50Hz PWM替代 |
| B06/B07/B09板上输出/注入/恢复 | NOT_RUN；只有镜像与主机决策结果 |
| B08资源/栈 | MAP资源PASS；真实Control/Idle栈余量NOT_RUN，无批准读出路径 |
| H10/B10带载 | NOT_RUN；不以本交接宣称通过 |

`C2_host_test.py` 使用MSVC14.42、C11、/W4 /WX /O2编译真实运行时代码，RTOS/寄存器/驱动为明确stub；不运行修改或替换版PWM驱动。主机fixture中的stack值与计数均不是硬件测量。命令/输出/返回码在`evidence/C2/host/results.json`，概览`host_results.txt`；真实ARMCC负向编译与静态检查在`compile_guard_results.json`、`verification.txt`。

## 人工执行与遗留

可执行的无SWD下载/接线/镜像/LA步骤，以及未来独立trace的测量边界、>=1000样本、候选19..21ms/<2ms与检测后关断<1ms提案，见`C2_hardware_guide.md`。本轮未批准或实现trace，所有外部测量为NOT_RUN；不得从PWM反推任务周期，也不把RAM中DWT/stack变量当作已报告的实测数据。

遗留：实际C1提交及上板记录不在本地；H10未补；C2所有板上波形、真实任务时序、关断时延、真实栈余量待测；无独立watchdog；本机编译器注册标签不符。没有已知编译失败或冻结资源冲突。**C2交付停在源码/构建/主机验证，P1b未ACCEPTED，不自动P2或push。**
