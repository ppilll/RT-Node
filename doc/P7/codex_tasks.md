# codex_tasks · 有序执行与验收清单

**用户授权范围：** 开发/构建/主机测试/交付脚本；禁止自动烧录、硬件操作、Stage1/Stage2、P6故障注入、Flash安全页修改。

## 本轮验收状态

C0-C5实现/主机/正式Stage0构建已完成，见[P07_HANDOFF](P07_HANDOFF.md)。40项原生检查与13项Python离线测试PASS，Keil5/ARMCC5 Rebuild 0 errors/0 warnings。下列完成项为开发/主机/构建范围，具体证据与限制以handoff为准；硬件、Stage1/2、Flash/IWDG注入始终NOT_RUN。正式CANC实际四字段57B，PERF3/4补充窗口和控制状态。

## C0 代码基线复核（先做，不单独拖成长阶段）

- [x] `git status`, `git rev-parse HEAD`, 当前代码同审查基线`c3e9408...`是否一致；对新版本差异简述。
- [x] 阅读 `control.c/.h/config.h`、`local_follow.c`、`imu_fusion.c`、`can_app.c`、`uart_app.c`、`health_*.c`、`main.c`、`display_app.c`、`p3b_config.h`、既有主机测试与 `doc/P3b/rk3588_test.py`。
- [x] 核查KEY0=STOP锁存、KEY1=OLED翻页、Stage0无实际Motion路径、Flash页`0x0807F800`、健康与控制门限。
- [x] 打印拟变更文件清单；当前文件存在未提交用户修改时保留原工作，不做`git reset --hard`、不清洁未知文件。

## C1 G-A手势算法主机回归

- [x] 检查已有`doc/P2b/tests/local_test.c`、`run_host_tests.py`与IMU Fusion测试；优先复用，不重复功能实现。
- [x] 覆盖100个静止样本标定、姿态零位、±2°死区、5°→15us稳定目标、±15°限角、1500±45us、2us/update、NaN/Inf、40ms样本TTL、generation/授权隔离。
- [x] 主机单元测试应区分纯函数已通过、Control/Policy模拟通过、硬件未测；更新/批注过时KEY0/KEY1测试提示。
- [x] 输出实际执行命令/通过数量/失败用例/编译器平台。**不得把G-A标成真实双轴机械控制通过。**

## C2 ControlTask P7性能窗

- [x] 10,000tick稳定预热后启动独立P7窗口，首个窗内采样只建基准。
- [x] `period_samples`、min/max周期cycles、max elapsed cycles、周期越19..21ms事件；即使ControlTask故障terminal也继续累计。
- [x] 无符号相邻DWT回绕，0样本、溢出、正好19ms/21ms边界、超边界、终态仍采集、getter并发快照测试。
- [x] 不改变原周期/截止期/clock/2000us保护及原`wake_count`/`late_count`语义，必须通过旧ControlTask host回归。

## C3 CAN/UART只读诊断

- [x] 从`CanApp_GetDisplaySnapshot().rx_count`提取已处理帧数，新增独立`qfail_events`和可定义的`overrun_event_count`，不消费原`s_rx_overflow`标志。
- [x] `PERF?`、`PERF2?`、`CANC?`可读，输出单位/长度与`measurement_plan.md`一致。
- [x] 用`4294967295`最大数字跑最坏长度测试：实际PERF/PERF2/CANC为56B/57B/57B含CRLF，**均<=64B**；检查`reply[64]`写越界/越界读/NULL、长字符串。
- [x] PING、COUNT?、HEALTH?、ADC?、ENV?、SAFE CLEAR两步现有功能全部主机回归。
- [x] 不加ControlTask串口打印、不加另一TX任务，不改ISR/队列仲裁/数据接收协议。

## C4 上位机双端脚本

- [x] `p7_uart.py --help`可运行，参数包含`--port`、`--mode baseline|load|boundary`、`--seconds`、`--out`、`--poll-seconds`、load模式`--ping-rate`。
- [x] `p7_can.py --help`可运行，参数包含`--interface`、`--rate`、`--seconds`、`--out`；只发合法ClassicalCAN 0x183 heartbeat。
- [x] 串口离线单测：混合PONG/ADC/HEALTH/PERF/P7N/CAN/COUNT；USB分块；CRLF/LF；超时、错误、错序与最大32bit十进制。
- [x] CAN离线单测：8字节心跳、16bit rollover、单调时钟不追发burst、错误日志，不把本地loopback算对端成功。
- [x] 输出JSON/CSV字段明确包含firmware版本、参数、开始结束时间、planned/actual、故障、原始/汇总；不能在无设备时生成假PASS。

## C5 构建/主机回归/交接

- [x] 正式 Stage0 Rebuild，记录0错误/0警告或如实报FAIL；无 Keil 明确NOT_RUN，不用单纯host clang/MSVC冒充 ARMCC5。
- [x] 配置核对Stage0/P6生产测试宏、MAP/HEX安全页范围，Flash=`Code+RO+RW`、RAM=`RW+ZI`；记录栈HWM读数待硬件测。
- [x] 运行已有主机测试与新增Parser/性能统计测试；一个真正失败的测试不能用“预期问题”掩盖。
- [x] 输出`doc/P7/P07_HANDOFF.md`，包含实际改动、已运行命令、实际结果、未运行项、编译宏、未解问题、人工命令、Stage0安全门禁。
- [x] 禁止未授权推送/PR、自动下载、CAN现场发送/串口现场动作、清理用户已有文件。

## 停止点（交给用户）

**交付到这里即可。** 我们收到Codex handoff后，另用硬件按 `operator_runbook.md`执行G-B/T1/T2，再审核P6既有证据，并决定是否追加单独授权的Stage1 G-C。没有用户现场数据，不填写好看的简历数字。
