# implementation_plan · Codex 最小修改架构

## 实际落地与保守决策

已按C1-C5实施。Control统计helper为App/control/p7_metrics.h，P7独立静态状态，不改旧诊断结构；59s以上tick间隔保守标无效，不把模差当可信周期。除建议PERF/PERF2外增加PERF3窗口/异常元数据和PERF4控制终态，以避免无法识别reset/无样本/故障。CANC增加d=HAL读取失败事件，最大57B。

主机入口为run_host_tests.py：复用P2b local/P2a fusion/P1b policy、适配当前KEY0/Health的P3b 16场景、P6a Flash-RAM控制和UART、P6b runtime；没有用旧夹具的KEY0启动假设改回生产代码。Stage1/2硬件镜像不构建。Python脚本参数需--firmware，另有UART --mode check和CAN显式--mode smoke --stage0-confirmed；默认CAN load只发heartbeat。生成物仅local_results/，实际命令及已知限制见P07_HANDOFF。

## 1. 设计约束

使用同一正式Stage0镜像测T1/T2；不要增加高频打印、trace task、软件定时器、动态内存或新协议。优先修改 `App/control/control.c/.h`、`App/can/can_app.c/.h`、`App/uart/uart_app.c`，加 `doc/P7/scripts/*.py` 和必要的主机测试。保持旧API二进制与语义相容。

**必须先出diff预览，再实施代码。** 用户已授权本范围的编码工作，无须再次征求对每个小文件的批准；若发现必须改变P6安全设计、Flash页、Stage0宏、执行器或CAN wire协议，请停止该危险分支，只提交其他安全内容并在handoff列风险。

## 2. ControlTask 计时插桩

**当前源码锚点：** `ControlTask()`入口读取`entry_cycle = DWT->CYCCNT`，原周期判断由RTOS Tick与DWT共同保证；末尾有`SampleStack()`，再读取`elapsed_cycles`、更新`worst_execution_cycles`，然后原执行超时逻辑及`xTaskDelayUntil()`。

- 在`control.c`内用静态存储维护`P7ControlMetrics`，不增加ControlTask栈上的较大局部数组。初始清零、窗口启动标记与首个合法入口样本状态明确。
- 用RTOS tick的`(uint32_t)(now - boot_tick) >= 10000U`开窗；首个窗内入口只建立DWT基准，不算跨窗口的首条差值；下次起每个入口计算`(uint32_t)(entry_cycle-previous_p7_entry_cycle)`。请注意`previous_cycle`已参与原clock故障验证，不得改变它的语义。
- 对所有窗内入口都更新P7独立周期极值、违例和样本计数，包括`session.terminal==true`后；此计数不得改变`Stop/Disarm`逻辑，原迟到故障判断必须仍在原位置。
- `P7 elapsed max`保持与原`elapsed_cycles`一致的起止点，所有窗内周期无论会话是否terminal均更新；不得在elapsed计时段加入阻塞查询/串口输出。
- Getter采用任务态短临界区整体复制；避免返回裸volatile结构引用。Window Start时的stats getter应显式返回`active=false`、`n=0`、极值未就绪，而不是伪造正常20ms。
- 对32位回绕和新计数器溢出的处理编写最小主机测试（可以在新纯C小helper层做断言；不能为了unit test大范围重构ControlTask）。任何新计时器对安全路径的影响需在`P07_HANDOFF.md`解释。

## 3. CAN事件统计

锚点：`ProcessFrame`入口`CanDisplaySnapshot.rx_count++`、ISR `xQueueSendFromISR`失败分支、`HAL_CAN_ErrorCallback`中FIFO overrun标志、`CanApp_TakeOverflow()`。

- `rx_count`既有语义不可更改：**已由CanTask出队进入ProcessFrame的帧**，包括最终被协议丢弃者；绝不能把它简单命名为“有效命令已处理”。
- 新增独立`qfail_events`，当`xQueueSendFromISR`失败时递增。可新增`rx_overrun_events`或`rx_error_callback_events`，只对实际检测到的事件递增并文档化重复回调的可能。
- 原`volatile s_rx_overflow`布尔事件仍在ISR置位、ControlTask读取后清除，**CANC getter不得调用TakeOverflow、不得clear标志**。保持NVIC/RTOS queue深度8/初始过滤器、状态发包策略不变。
- 新getter在任务上下文短临界区复制必要字段；CANC格式不要挤进CAN STATUS线上8字节。

## 4. USART1三个短只读查询

- 实际实现`PERF?`（周期min/max、elapsed max、违例）、`PERF2?`（采样数、Control/Idle/Health栈剩余words）、`CANC?`（CanTask processed、queue fail事件、RX overrun事件），实际加入CANC d=HAL读取失败后，长度为56B/57B/57B上界，详见`measurement_plan.md`。
- 继续使用原UART任务的`reply[64]`与`SendReply()`。当所有32bit数取`4294967295`时必须由host测试实际通过；一条命令一条回复，不挤掉每200tick的ADC自动报告。格式不变更 `COUNT?`、`PING`、`HEALTH?`、`SAFE CLEAR?`、`SAFE CLEAR YES`、`ADC?`、`ENV?`。
- 不从ISR/ControlTask直接格式化或发送；使用原整数append函数或新增受边界检查的紧凑函数（禁`printf`）。每5秒读取性能信息足够，避免轮询造成系统新负载。
- 失败/尚无数据时输出清晰固定结果并由上位机标`NOT_READY`；所有聚合读取接口必须是**纯观测**，不执行状态/计数复位或安全动作。

## 5. 可执行脚本规格

### `doc/P7/scripts/p7_uart.py`（用户 Windows PC）

实现CLI，例如：

```
python p7_uart.py --help
python p7_uart.py --port COM5 --baud 115200 --mode baseline --seconds 120 --poll-seconds 5 --firmware "$FW" --out p7_t1a.json
python p7_uart.py --port COM5 --baud 115200 --mode load --seconds 600 --ping-rate 10 --poll-seconds 5 --firmware "$FW" --out p7_t1b_uart.json
python p7_uart.py --port COM5 --baud 115200 --mode boundary --seconds 30 --firmware "$FW" --out p7_t2_uart_boundary.json
```

Codex应落实以上参数或输出**同等清晰可执行命令**；依赖仅Python3+pyserial（提供`requirements.txt`若需）。需要做到：

- `PING\n`按单行发送，`PONG`按完整行分类；设备自动ADC报告、HEALTH/PERF/CAN/COUNT/ENV应被归类而非误作PONG；一个慢回复可以跨多次USB读取。
- 给计划发包与实际串口write成功/timeout分类；不凭未带ID的PONG断言“每个PING唯一对应”。设合理超时与容许少量设备启动前预热，**记录实际发送次数与测量时长**。
- 在10s预热完成（PERF2 `n>0`）后启动正式计时；记录`HEALTH?`开始和结束、`COUNT?`开始和结束、`PERF?`/`PERF2?`/`CANC?`每5s和最终结果。指令不能和另一PC串口助手同时写入串口。
- 边界模式将一个`PING`分为多段写，复用`\n`/`\r\n`换行，并发送若干完整命令使累计接收字节超过256（不强制一次连续爆发>256）。不能往序列里放`SAFE CLEAR?`或未知运动命令。
- 记录raw rows CSV和汇总JSON（时间、命令类、诊断数、所有异常）；支持离线Parser unit tests（本机无串口也能验证交错/截断/最大32位字段）；退出时打印人类可读摘要，不宣称PASS若无设备。
- 可选检测健康异常并停止发送输入，但绝不自动调用`SAFE CLEAR`、硬件复位或烧录。

### `doc/P7/scripts/p7_can.py`（RK3588 Linux）

实现CLI，例如：

```
python3 p7_can.py --help
python3 p7_can.py --interface can0 --rate 50 --seconds 600 --firmware "$FW" --out p7_t1b_can.json
```

要求：

- **仅在正式Stage0长测发送合法`0x183` HEARTBEAT帧**，8B载荷以`<BBH4x`(version=1,mode=0,16bit递增counter)打包，使用Python标准库`socket.AF_CAN` + `CAN_RAW`，现有`doc/P3b/rk3588_test.py`可做实现参考；不可新建或更改CAN线上协议。
- 单调时钟节拍；落后时只记录落后并按时钟恢复，**不追发积压成burst**。记录计划、实际send()返回成功、失败、发送间隔、运行时间；可选监听设备`0x280`响应并计数，但本地loopback/发送调用成功都不等于MCU已收到。
- 不自动执行 `ip link set`、`sudo`、配置CAN接口、Mode/STOP、改变Flash/Stage，保留物理接口初末`ip -statistics`人工记录。
- `--help`、参数边界、无硬件时的格式/节拍逻辑纯Python测试。

**同步约束：** 两台主机没有已证实同步的单调时钟；从各自主机按人操作近同时启动并记录时间区间，不能宣称它们严格同一瞬间开始。CAN处理计数的起点在发送前由PC记录、发送结束后排空队列再取终值；把起止查询竞态/可能背景帧作为误差来源。

## 6. 回归与交付路径

建议在仓库新增：

```
doc/P7/
  README.md
  measurement_plan.md
  implementation_plan.md
  test_plan.md
  codex_tasks.md
  P07_HANDOFF.md
  scripts/p7_uart.py
  scripts/p7_can.py
  scripts/test_p7_parsers.py     # 如需要
  scripts/requirements.txt     # pyserial only, if needed
```

可创建一个`doc/P7/local_results/`目录（gitignored），但不允许提交由板卡生成的所谓“实测通过”数据或大型日志目录。必要时将本包文本复制到对应`doc/P7`文档，**按实际实现更新**，不得直接宣称模板内容已经执行。

主机测试：优先既有 `doc/P2b/run_host_tests.py`、P3b脚本、`doc/P6b/tests/run_host_tests.ps1` 等，以**本地存在**的入口为准。Keil正式Build在具备ARMCC5的Windows环境执行，保存build日志与MAP；Codex没有Keil应只生成命令与`NOT_RUN`状态。

## 7. 出错后的边界

- 编译或已有单测失败：先修复插桩导致的回归，不能通过修改安全逻辑“修测试”。
- UART超64B、脚本解析混乱：调整只读回复布局、再补最大值测试；不能扩张为复杂通用日志系统。
- P6安全锁、Option Bytes、Stage1输出路径有任何不确定性：停止对应硬件操作，交由用户确认，继续执行安全的host部分。
- T1/T2数据异常：保留原始记录；一次受控优化/复测须经用户确认，不默认进行压力极限探索。
