# P07_HANDOFF · Stage0 开发交接

2026-10-19，Asia/Shanghai。**代码就绪；主机验证 PASS；正式 Stage0 Rebuild PASS；实机未执行（NOT_RUN）。**
T1/T2/G-B 的真实性能、通信、IMU及栈余量均为 **待测**。G-C Stage1、Stage2、Flash/IWDG故障注入均未执行；没有创建或填写 `P07_RESULTS` / `P07_RESUME_EVIDENCE`。

## 基线和保留的工作区

- HEAD：`c3e9408013ae009e2b6cb49ac8d53f79d169e430`，与批准包一致；本次是未提交工作树，未推送。
- 进入时已有 `doc/DECISIONS_AND_LIMITS.md`、`doc/P06_CODEX_MASTER_PROMPT.md`、`doc/README.md`、旧P6 ZIP、`doc/references/{official_links,source_audit}.md` 删除，以及未跟踪的 `doc/P7/`。全部保留，未重置/恢复用户改动。
- 固件身份以 `local_results/stage0/source_hashes.json` 与 HEX SHA256 为准，不能只用未变化的 HEAD 表示新固件。

## 文件级改动与代价

| 文件 | 改动 / 理由 |
|---|---|
| `App/control/control.c` | 静态 `s_p7`；无条件入口统计和既有 elapsed 值归档；短临界区整体 getter。旧 `ControlDiagnostics`、late_count、故障判定和延时保持原样 |
| `App/control/control.h` | 新只读 getter；旧结构/API未改 |
| `App/control/p7_metrics.h` | 小型固定状态/helper，预热、相邻模差、边界、无效间隔与饱和；无堆、日志、任务或通用 trace |
| `App/can/can_app.c/.h` | 独立累计 queue-send失败 / overrun-bearing回调 / HAL读取失败事件；getter不读取或清除 `s_rx_overflow` |
| `App/uart/uart_app.c` | 原 UartTask 单一TX内新增5个只读查询，固定四字段格式化，原64B缓冲/30B分块保持 |
| `doc/P2b/tests/local_test.c` | 只补±1.9/±2度、5度→15us和完整1500→1530的2us步序；未改算法 |
| `doc/P7/run_host_tests.py`, `tests/{control_metrics_host.c,uart_extra.c,can_events_host.c,queue.h}` | 复用 P2b/P3b/P5b/P6a/P6b 真实C与模拟入口；适配已过时的夹具，增加计数/并发/回复边界测试 |
| `doc/P7/scripts/{p7_uart.py,p7_can.py,test_p7_parsers.py,requirements.txt}` | 串行UART采集、SocketCAN发送/显式烟测、离线测试；pyserial为唯一串口依赖 |
| `doc/P7/rebuild.ps1`, `scripts/check_build.py`, `.gitignore` | 隔离正式Stage0 Rebuild、有效宏/预处理/MAP/HEX校验；生成物留在忽略的local_results |
| `doc/P3b/rk3588_test.py` | 纠正 `--end local` 危险启动提示，线协议未改 |
| P2b README/HARDWARE_TEST_GUIDE/STAGE2_BUILD/test_plan；P3b README/STAGE1_TEST_GUIDE/test_plan/interfaces | 页首标明历史内容过时、KEY0=STOP锁存、KEY1=OLED翻页，转到当前指南 |
| P7 README/source_audit/measurement_plan/implementation_plan/codex_tasks/test_plan/operator_runbook/本文件 | 更新实际实现、验证状态、执行命令和人工停点；批准参考原文及空模板保留 |

计时点：入口仍先读 `DWT->CYCCNT`，随后读 tick 并调用 `P7Entry`；每次控制循环（包括terminal后）都观察。`SampleStack()` 仍在原位置，随后原 `elapsed_cycles = DWT->CYCCNT - entry_cycle` 不变。原 worst/2000us 判断先完成，再将同一个 elapsed 值交给 `P7Elapsed`。入口统计开销包含在 elapsed 中；elapsed归档、原测量点后的处理及 getter 不包含在该 elapsed 中。周期样本包含两次入口之间全部运行/抢占/睡眠影响。**不是纯CPU WCET，也不是PWM周期。**

新增固定RAM在MAP中体现为总ZI增加64B（相对旧P6记录），没有改任务栈配置。每周期新增常数次32位运算/比较/写入，没有除法、格式化或UART调用。getter复制8个32位指标和一个违例计数；UART低优先级读取临界区期间会短暂推迟调度，具体周期影响仍需硬件测量，不编造us开销。Host线程锁模型只验证临界区整体复制，不证明Cortex-M3硬件时序。

## 查询契约（ASCII，十进制uint32，含CRLF）

| 请求 | 实际输出 | 最大字节数 |
|---|---|---:|
| `PERF?` | `P7 a=<min_cycles> b=<max_cycles> x=<max_elapsed_cycles> v=<violations>\r\n` | 56 |
| `PERF2?` | `P7N n=<period_samples> c=<control_stack_words> i=<idle_stack_words> h=<health_stack_words>\r\n` | 57 |
| `PERF3?` | `P7W t=<window_start_tick> u=<last_observed_tick> f=<flags> d=<invalid_samples>\r\n` | 57 |
| `PERF4?` | `P7S m=<old_control_mode> f=<old_control_fault> w=<old_wake_count> l=<old_late_count>\r\n` | 57 |
| `CANC?` | `CAN r=<processed_rx_count> q=<queue_fail_events> e=<overrun_callback_events> d=<read_fail_events>\r\n` | 57 |

全部四数均为4294967295时经过真实UART C测试，含CRLF、无需NUL，均小于原64B。原 `COUNT?` / `HEALTH?` / `PING` / `ADC?` / `ENV?` / 两步SAFE恢复语义保留；**新脚本从不发送SAFE命令**。

- PERF3 flags：bit0=已开窗，bit1=计数器饱和（尝试超出UINT32_MAX），bit2=出现无效观测；其余位0。新增P7计数饱和，不改变旧计数自然回绕。CAN新增事件UINT32_MAX视为下限。
- 以Control调度epoch为起点，经过10000tick开窗；首个窗内入口只建基准，`n=0`时a/b=0。t/u为32位1kHz tick；u是最近Control入口，不是串口回复时刻。
- 相邻 `uint32_t` DWT差，19ms=1368000、21ms=1512000，等于边界不计违例。间隔tick>=59000、时钟禁用或零cycle差不计合法样本，d累计并标无效；仍更新基准以恢复后续观察。单个tick整圈(~49.7天)停顿无法消歧，不能用于这样的工况。
- r复用原CanTask已出队进入ProcessFrame计数，包含最终被拒帧，自然uint32回绕；q为队列投递失败；e为**带FOV0历史标志的HAL错误回调次数**，可因sticky error重复累计；d为HAL读取失败。均不能叫精确丢帧数。
- 各查询不是同一时刻的全局原子快照；CSV保存逐条主机时间，允许n/极值等跨查询变化。栈是自启动以来最低剩余words，32位字长时×4B。
- 极值自t起累计，不是主机任意120/600秒区间可相减的极值；查询、启动延迟及尾部排空都可能扩大观测窗。必须同时报告t/u/n、主机定时区间和首末快照。A/B长度不同，不能据极值差写优化百分比。

## 实际验证

| 项目 | 状态 | 实际证据 |
|---|---|---|
| G-A映射/融合/Policy | PASS（主机） | ±sign映射、边界/限幅/限速、NaN/Inf、TTL39/40、generation/授权切换；真实fusion100样本标定/中断/数值拒绝；不代表板上PWM |
| P3b控制回归 | PASS（主机） | 16个Stage0场景，真实ControlTask循环；fault延长至12.2s，10s后terminal仍计样本/违例，旧late_count仍为1 |
| P6安全/健康回归 | PASS（主机） | 重用Flash-RAM模拟、锁/恢复/KEY0/KEY1、HealthTask监督/喂狗/HAL异常/宏守卫；未访问设备或物理Flash |
| P7计量/并发 | PASS（主机） | 零样本、精确边界、单cycle越界、DWT/tick回绕、59s无效间隔、计数饱和；50000次并发getter一致性 |
| UART/CAN新接口 | PASS（主机） | 真实UART最大值/0/哨兵/CRLF；旧命令/SAFE恢复/HEALTH组合；真实CAN ISR事件/饱和/getter不消费overflow/被拒帧rx_count |
| Native入口合计（Windows x64 / MSVC cl 19.42.34435.0） | PASS | `python doc/P7/run_host_tests.py`：40项编译/运行/负向检查（含25次原生程序执行）；`local_results/host/results.json`、`local_results/host_console.txt`、`local_results/host_compiler.txt` |
| Python离线 | PASS | 13项unittest：交错、USB分块、最大值、错序/超时/错误、节拍、loopback、烟测打包/最终STOP、模拟30s串口全流程、无设备/依赖缺失错误落盘；`local_results/python_tests.txt` |
| 两个 `--help` | PASS | 不需要pyserial或SocketCAN设备 |
| 真实pyserial/SocketCAN IO | NOT_RUN | 本解释器未安装pyserial；Windows未运行Linux CAN；未打开真实端口/发送现场CAN |
| 正式Stage0 Rebuild/安全页检查 | PASS | 以下构建记录；不是实机执行 |
| G-B/T1-A/T1-B/T2与本轮P6实机核验 | NOT_RUN | 待用户现场执行/提供原始P6回执；旧P6a概括完成声明为USER_REPORTED，逐项R无原始回执可升级为PASS |
| G-C Stage1 / Stage2 / Flash/IWDG注入 | NOT_RUN | 未授权、未构建这些硬件固件、未下载/执行 |

原P2b/P3b入口不能原样证明当前控制器：按键语义和Health接口已变。P7入口复用其local/fusion/policy/16场景，生成当前GPIO/Health适配夹具；不修改历史证据。P6b的T2宏仅在原生模拟进程中覆盖监督测试分支，**没有生成/执行T2硬件镜像**。旧Stage1原生/固件分支本轮不执行。

初期测试夹具曾FAIL：嵌套main宏冲突、COUNT fixture未清DMA/overflow状态、CAN_Init时scheduler状态错误，均已修正并重跑通过，`local_results/host/attempt_*.json`保留失败记录。ARMCC宏探针首版绝对UTF8 include不兼容中文路径，改为相对ASCII include后通过。未通过放宽生产安全逻辑解决问题。

P6证据复用审阅：`../P6a/P6a_HANDOFF.md`明确其硬件项NOT RUN；`../P6b/P6b_HANDOFF.md`转述用户“测试完成(日志不更新)，接着去完成P6b”。该概括声明仅记 **USER_REPORTED（P6a）**，没有逐项RCC/LOCK/NRST/POR/恢复原始日志，不能推导P6b实机R1-R6通过。原始回执复核仍NOT_RUN；本次不重新注入或清锁。

## 正式Stage0构建记录

- 2026-10-19 14:47:03–14:47:16（Asia/Shanghai），Keil MDK Plus 5.36，ARMCC 5.06 update 7 build960；Rebuild exit0、0 errors / 0 warnings。
- 显式宏：`P3B_OUTPUT_STAGE=0,P2B_OUTPUT_STAGE=0,P6_WDG_TEST_ENABLE=0,P6_WDG_TEST_UNLOADED_ACK=0,P1A_ACTUATOR_TEST_ENABLE=0,P1A_AUTOTEST_ENABLE=0,P1B_AUTOTEST_ENABLE=0`；保留 `STM32F103xE,USE_HAL_DRIVER,BME280_64BIT_ENABLE`。
- ARMCC真实预处理核对：上述宏和20tick/2000us/300tick有效；Control `Motion` / `Apply` 定义不在Stage0预处理输出中。
- MAP：Code **44702**、RO **1062**、RW **248**、ZI **16512** B；Flash=`Code+RO+RW`=**46012 B**；静态RAM=`RW+ZI`=**16760 B**。
- Intel HEX每条校验和/每个数据字节校验PASS：`0x08000000..0x0800B3BB`；安全页`0x0807F800..0x0807FFFF`完全无数据。**这不能证明下载器不会全片擦除。**
- HEX SHA256：`58fb1abf1eceb1ec54f303fbdf8b64cbebc2b134a4119341f11ef4fb030e7516`。
- 产物：`local_results/stage0/P7_STAGE0.hex`、`.axf`、`Listings/P7_STAGE0.map`、`rebuild.log`、`compiler_version.txt`、`build.json`、`validation.json`、`effective_macros.txt`、`control_preprocessed.c`、`source_hashes.json`。生成物本地忽略，不复制为假实测结果。
- 未改 `App/health/*`、`App/actuator/*`、`Core/*`、`Driver/*`、原工程配置或任何控制/健康配置值；无新任务/动态堆分配。

## 可复制执行的命令

以下从仓库根目录运行。除host/build/help外，其余是**用户后续现场执行**，本次没有执行。COM5需人工替换，输出名已存在时脚本拒绝覆盖，重测请换名。RK拷贝P7脚本目录即可，无需在线调试器。

### 1. 本机复现与现场安全核查

```powershell
python doc/P7/run_host_tests.py
python -m unittest discover -s doc/P7/scripts -p test_p7_parsers.py -v
powershell -NoProfile -ExecutionPolicy Bypass -File doc/P7/rebuild.ps1
python doc/P7/scripts/p7_uart.py --help
python doc/P7/scripts/p7_can.py --help
# 以下依赖安装与现场命令交给用户：
python -m pip install -r doc/P7/scripts/requirements.txt
$port = 'COM5'
$fw = (Get-Content doc/P7/local_results/stage0/validation.json | ConvertFrom-Json).hex_sha256
Get-FileHash doc/P7/local_results/stage0/P7_STAGE0.hex -Algorithm SHA256
python doc/P7/scripts/p7_uart.py --port $port --mode check --firmware $fw --out doc/P7/local_results/operator/check_a.json
```

先人工断开舵机电源及PC6/PC7舵机信号，确认正式Stage0及P6状态；下载方法必须确认保留安全页，**不提供自动烧录/复位命令**。DTR/RTS禁止连到复位/BOOT控制；脚本置其为deassert也不能保证所有适配器开端口无毛刺。只许一个串口写端，10s开窗n>0后脚本才开始正式定时。HEALTH要求C=RUN/W=ON/T=0；L/P/A需操作者按真实持久策略记录，LOCK/EMPTY/BAD不解锁。

```bash
# RK只读；500000bit/s Classical CAN、UP、非bus-off，单一0x183发送者
ip -details -statistics link show can0
python3 doc/P7/scripts/p7_can.py --help
FW=58fb1abf1eceb1ec54f303fbdf8b64cbebc2b134a4119341f11ef4fb030e7516
```

若接口不符合，停止并由用户按现有已验证接线/流程人工配置；脚本从不执行ip link set或sudo。

### 2. T1-A 120s（无持续CAN负载）

用户按安全方式独立重启同一Stage0，保留Flash页，预热10s；不要按KEY0启动。

```powershell
python doc/P7/scripts/p7_uart.py --port $port --mode baseline --seconds 120 --poll-seconds 5 --firmware $fw --out doc/P7/local_results/operator/t1a_uart.json
```

### 3. T1-B/T2 600s联测

再次由用户安全重启同一镜像并预热10s。PC先启动；看到 `READY`（已记录初始CANC/COUNT）再启动RK，记录起止偏移。两端没有共享单调时钟，不声称严格同步。

```powershell
python doc/P7/scripts/p7_uart.py --port $port --mode load --seconds 600 --ping-rate 10 --poll-seconds 5 --firmware $fw --out doc/P7/local_results/operator/t1b_uart.json
```

```bash
python3 doc/P7/scripts/p7_can.py --interface can0 --mode load --rate 50 --seconds 600 --firmware "$FW" --out p7_t1b_can.json
ip -details -statistics link show can0 > p7_can_after.txt
```

双方结束后、RK停发至少1s，由PC再做只读终值检查。不能在RK仍发送时拿PC提前结束的CANC作总终值：

```powershell
python doc/P7/scripts/p7_uart.py --port $port --mode check --firmware $fw --out doc/P7/local_results/operator/t1b_after_rk.json
```

处理增量采用PC `t1b_uart` 首快照CANC.r与 `t1b_after_rk` 末快照CANC.r的uint32差，对比RK successful；确认发送前后没有额外0x183/其他被滤波接受帧、没有重启并报告查询竞态。只能称处理比例，不能称精确线层丢包率。共同负载覆盖区间需按日志解释，不把设定50/10当实际速率。

### 4. T2-U 30s分段/累计回绕与 T2-C 30s协议烟测（分别执行）

```powershell
python doc/P7/scripts/p7_uart.py --port $port --mode boundary --seconds 30 --ping-rate 10 --firmware $fw --out doc/P7/local_results/operator/t2u_boundary.json
```

PING拆成两次写、交替LF/CRLF，低频COUNT等查询交错；自动ADC完整分类；检查RX增量>256、PONG及OVF。这里是DMA累计跨环测试，不制造一次性>256B突发溢出。

先用UART check人工确认Stage0、C/W/T正常、KEY0未锁存、P6策略允许普通MODE，当前STATUS为DISABLED。LOCK/EMPTY/BAD或MODE拒绝则记策略阻断，不清锁；跳过依赖授权的烟测。

```bash
python3 doc/P7/scripts/p7_can.py --interface can0 --mode smoke --stage0-confirmed --seconds 30 --firmware "$FW" --out p7_t2c_smoke.json
```

显式smoke：读STATUS.action/epoch→MODE REMOTE→验证WAIT_REMOTE/未enabled→递增seq中心及1510/1500目标、固定20ms→finally尝试GLOBAL STOP并等待DISABLED。默认load绝不发送MODE/CONTROL/STOP。烟测异常可能导致最终STOP也失败，异常上下文保留；不自动重试或授权。**仅证明Stage0协议/仲裁部分路径，不能证明Motion/PWM。** `--stage0-confirmed`是操作者核查声明，不能从CAN自行识别编译Stage。

## 采集结果与人工停点

脚本原始CSV逐条flush，JSON包含参数/firmware身份、UTC+monotonic区间、计划请求/成功写/失败、PING/PONG/超时、分类计数、聚合快照/增量和异常。CAN另有nominal_slots、skipped_slots、成功send间隔、接口初末统计/错误变化、外部STATUS。落后跳过时隙，不追发burst；send成功不是MCU收到，MSG_DONTROUTE本地帧不作对端证据。退出0和`CAPTURED`仅说明完成采集，hardware_verdict始终待人工审核，不是自动硬件PASS。

超时/错序/ERR/乱码、Control fault、非RUN/ON、计量超界/饱和/无效、UART OVF或CAN接收事件会停止UART输入；CAN发送错误/bus-off/FAULT/接口错误增长会保留FAIL。脚本不会跨机自动停止另一发送端；任一端异常时用户立即停止另一端，保留日志，不自动复位/清锁/放大负载。SIGKILL/断电不能保证JSON finally完成，已flush CSV可保留；应使用Ctrl+C。

仍需人工确认：下载器保留末页、Option Bytes/IWDG启动前提、真实COM/共地/终端电阻/500k链路、DTR/RTS接线、单发端、真实固件SHA、启动/测量窗口对齐及P6既有回执。栈HWM、无意外复位/Control终态、真实IMU姿态输入全部待测。G-C须重新独立批准并由用户执行，Stage2未批准。**交接停止在用户实机测试之前。**
