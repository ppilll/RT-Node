# operator_runbook · Codex完成后的现场操作（无SWD/J-Link）

## 当前脚本的权威命令

请先按[P07_HANDOFF的完整命令](P07_HANDOFF.md#可复制执行的命令)设置 `$port`、`$fw`，RK设置`FW`。两个脚本均要求 `--firmware`；JSON旁边自动写CSV且拒绝覆盖已有文件。UART新增`--mode check`只读核查，CAN烟测只能显式 `--mode smoke --stage0-confirmed`，默认load只发0x183。

本轮构建/主机验证PASS，实机NOT_RUN。`CAPTURED`代表采集完成，人工审核后才能给测试PASS。PC先启动，看到READY初始CANC已记录后再启RK；两端结束、RK排空1s后必须再运行UART check取得最终CANC，避免PC比RK早结束少算。窗口极值自开机10s累计，尾部排空/查询也在观测窗内，需记录t/u/n，不把它说成严格120/600s限定极值。

PC先安装 `python -m pip install -r doc/P7/scripts/requirements.txt`。确认DTR/RTS不连NRST/BOOT，单一串口写端；脚本置DTR/RTS去使能不保证所有转接器无开端口毛刺。任何一端异常要人工停止另一端，脚本不跨主机联锁。


> 这是**以后由用户在硬件上实际执行**的指令模板，不代表已经运行。所有“建议命令”以Codex在`P07_HANDOFF.md`列出的实际脚本参数为准。涉及电源/烧录前由用户复核自身硬件连接与P6安全页保护。Stage1须单独授权，Stage2未批准。

## 0. 收到Codex交接后先检查

- [ ] 代码变更只涉及P7统计、只读UART/CAN接口、主机脚本、测试文档/必要工程配置；P6锁/Control门限/Actuator/Stage宏未改。
- [ ] `doc/P7/P07_HANDOFF.md`给出工作版本、编译宏、正式Stage0的Keil构建结果（或诚实的NOT_RUN）及新host测试。
- [ ] Keil/ARMCC5 `Rebuild`实际为0 errors/0 warnings；编译镜像`P3B_OUTPUT_STAGE=0`, `P6_WDG_TEST_ENABLE=0`，旧运动测试宏关闭；HEX内容未延伸至`0x0807F800`安全页。
- [ ] 使用的串口下载工具**不执行全片擦除**；如果工具会清除末页或无法确认，停止，先解决安全页保留问题。不要为了重置计数器mass erase或绕过P6锁。
- [ ] 断开舵机供电和PC6/PC7舵机信号，保持无需带载运动；确认仪器与MCU共地时只按现有已验证方案接线。

## 1. 现场只读初始化

1. 烧录经核实的**正式Stage0**镜像并以原方法启动；记录操作者、测试固件commit/构建时间/编译宏、上电时间。没有安全烧录方法则**停止烧录**。
2. PC/CH340连接正确端口，115200/8N1；仅打开一个能写串口的程序。发`HEALTH?`，记录完整`C/W/R/L/P/A/T`。如果`C=STALL`、`W=STARTERR`、不可解释的RESET、授权策略异常，**停止施加负载**。
3. 发`PING`应收到`PONG`；`COUNT?`应得到`RX=...,OVF=...`；由Codex新增的`PERF?`, `PERF2?`, `CANC?`得到可解析数字，窗口前`n=0`是正常，10秒后应`n>0`。OLED可切IMU页检查更新。
4. RK3588: `ip -details -statistics link show can0`只读检查500000bit/s/非bus-off；其他程序不能持续发送与本次统计同类的0x183。仅在确实需要且总线无其他用户时，人工配置：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 fd off
sudo ip link set can0 up
ip -details -statistics link show can0
```

> CAN端无需执行`sudo`配置如果链路已正常。`candump`显示来自本地的报文不等于STM32处理，CAN成功发送只证明Linux API接受发送调用。

## 2. G-B：ICM20602实机输入观察（Stage0）

- 持稳ICM20602约2秒，OLED显示IMU页，确认READY/有效性以及Roll/Pitch大致稳定；静置30秒，人工记录读数波动方向而不是臆造精度。
- 缓慢做左右滚转和前后俯仰各3次，回中，观察角度方向与变化；若传感器进入FAILED/不更新，停止其余复杂测试先排查。
- 取一张OLED状态图片或简短视频记录环境、动作与实际结论。此时Stage0 **PC6/PC7不得输出PWM**；不据此声称机械云台已跟随。

## 3. T1-A：120秒基准负载

- 建议单独安全重新启动Stage0（保留持久Flash），等待10秒预热；先读一次`HEALTH?`，确保没有误复位与异常。
- 根据Codex提供的实际CLI，在PC执行类似：

```powershell
python doc/P7/scripts/p7_uart.py --port COM5 --baud 115200 --mode baseline --seconds 120 --poll-seconds 5 --firmware $fw --out p7_t1a.json
```

`COM5`**只是占位示例**，必须改为本机真实端口。在此窗口**不要**启动RK CAN持续负载；保留IMU/ADC DMA/BME280/OLED/Health运行和正常ADC自动报告。退出后确认输出包含起止健康、窗口samples、最小/最大周期、最大elapsed、违例数、栈剩余。

## 4. T1-B + T2-C/U：600秒联测

- 推荐再次安全启动**同一正式Stage0镜像**，10秒预热后开始。不能改变编译选项、IWDG或Flash锁，不换硬件。
- PC启动`p7_uart.py` load模式（目标10 PING/s），RK3588近同时启动`p7_can.py`（只发`0x183`，目标50帧/s）。两端实际启动并非严格同步，记录各端wallclock及monotonic起止，结果以各端运行日志及窗口对齐说明为准。

```powershell
# PC（端口示例需替换）
python doc/P7/scripts/p7_uart.py --port COM5 --baud 115200 --mode load --seconds 600 --ping-rate 10 --poll-seconds 5 --firmware $fw --out p7_t1b_uart.json
```

```bash
# RK3588（先确认can0已正常）
python3 doc/P7/scripts/p7_can.py --interface can0 --rate 50 --seconds 600 --firmware "$FW" --out p7_t1b_can.json
ip -details -statistics link show can0
```

- RK停发后等待约1秒让CAN任务排空，再由PC读取终态`CANC?`、`HEALTH?`、`PERF?`/`PERF2?`、`COUNT?`。**不能把主机send数量直接叫STM32处理数**；如果主机同步性不够，报告可比区间和可能背景帧。
- 检查`period_min`、`period_max`、`elapsed_max`、`violations`、stack HWM、IWDG/Control状态，以及PONG、RX/OVF、CanTask处理数与queue事件。保存JSON/CSV以及明显异常时的原始终端行。

## 5. T2短测：UART边界与CAN协议烟测

**UART短测：** 仅在Stage0，30秒，使用Codex脚本`--mode boundary`，将PING分段、混合LF/CRLF、累计>256B跨环形DMA缓冲，记录PONG、COUNT RX/OVF；避免过载到持续丢字节。默认没有恢复/运动命令。

```powershell
python doc/P7/scripts/p7_uart.py --port COM5 --baud 115200 --mode boundary --seconds 30 --firmware $fw --out p7_t2_uart_boundary.json
```

**CAN协议烟测：** 依实际0x280 STATUS读取epoch/action再构造合法MODE/CONTROL/STOP；Stage0可验协议/WAIT/STOP，**不出现ACTIVE/PWM输出**。现有`rk3588_test.py remote`要求实际ACTIVE，在Stage0可能预期报错；不要用它代替Stage0专用烟测。若Codex创建明确的`smoke`模式，先检查它只包含已批准的合法帧且最终STOP。若P6锁为LOCK/EMPTY/BAD，MODE拒绝是安全设计，不能为让烟测“通过”清锁；记`BLOCKED_BY_POLICY`并保存状态。

## 6. R P6记录复用与M资源

从之前的实机回执整理IWDG ON、真正IWDG重启、R/L/P、NRST/POR锁状态（如已测）、SAFE恢复MANUAL的证据。没有日志的条目写`USER_REPORTED`而不是PASS。不要再次主动停喂/清安全页制造数据。

MAP取`Code, RO, RW, ZI`：Flash=`Code+RO+RW`、静态RAM=`RW+ZI`；Control/Idle/Health stack HWM以words记录，乘4字节仅作为32位StackType_t换算。

## 7. G-C Stage1 LA门禁（当前不执行）

后续必须由用户单独明确批准：舵机独立供电**断开**、PC6/PC7与舵机信号线**断开**、LA CH0/CH1只接这两个引脚，确认烧录的是Stage1而不是Stage2，确保P6锁状态合法；观察低电平/中心/≥40ms priming/50Hz/手势小幅映射/STOP。历史“KEY0启动/KEY1 STOP”不适用于当前P5/P6。没有此次额外批准一律`NOT_RUN`，绝不可偷用Stage2真舵机带载。

## 8. 异常立即停止条件

- 任何舵机/执行器意外通电、Stage宏不明、安全页可能被擦除、编译产物超安全页。
- `HEALTH?`报告`C=STALL`、`W=STARTERR`或突然复位，CAN bus-off/显著错误持续增长、UART接收系统性乱码/OVF激增、Control异常进入FAULT。
- CODEX输出只有构建日志，没有实机证据，却声称T1/T2真实PASS。
- 发现KEY0被当作本地启动、KEY1被当作STOP、或普通串口命令可以清P6锁。

异常时停止外部发包并保存日志，不改门限、不过载重测、不自动恢复授权；我会根据具体记录决定是否需要一轮针对性排查。

## 9. 将以下材料发回当前对话

- `doc/P7/P07_HANDOFF.md`；Stage0 HEX/MAP 构建结果及主要统计接口格式。
- G-A主机输出，G-B照片/短视频（可选），T1-A与T1-B UART JSON/CSV、RK CAN JSON、T2短测记录；从P6已有实机回执中提炼的`HEALTH?`样本。
- 每项只报真实`PASS/FAIL/NOT_RUN/USER_REPORTED`，不需要大量重复截图或凭空追求100%好看数字。
