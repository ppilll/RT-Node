# P6b 三组核心实机测试（仅三组）

**注意**：用户没有 ST-Link/J-Link。使用 CH340 串口下载/通信、PC 串口助手、RK3588 SocketCAN、OLED、逻辑分析仪。起始全部测试优先 `P3B_OUTPUT_STAGE=0`，舵机独立电源关闭，避免带载热插拔 I²C/SPI 器件。任何带载测试需要另行批准。

**实现后的字段映射**：HEALTH? 的 C/W/R/L/P/A/T 分别表示 CTRL/WDG/RESET/LOCK/POLICY/本次自动LOCAL许可/T2停喂；W可为OFF/START/ON/STARTERR，C为INIT/RUN/STALL。后文长字段是概念名，以该短字段解析。这里仅给预期，不是实机记录。

**镜像**：正式候选 `evidence/production_stage0/P6b_PRODUCTION_STAGE0.hex`；专用T2 `evidence/t2_stage0/P6b_T2_STAGE0.hex`。均为无输出Stage0，仅完成编译，未下载。执行前须核对电源/接线。生产没有运行时停喂命令。

**T2额外前提**：当前持久策略须为EMPTY或AUTO且控制Disabled；MANUAL/LOCK/BAD不注入，不能为重做T2把它们恢复为AUTO。启动IWDG约5s后才锁存停喂（T=1），W仍为ON表示硬件已开启。IWDG后已持久化LOCK使后续NRST/POR均不再注入。验证期间保留安全页，不能把mass erase重新下载后的状态当作跨复位锁保持证据。实际超时随LSI变化，从最后喂狗起估计1.33..2.67s，不是从开机起2s。

## T1：健康状态 + 局部故障

**操作**：烧录正式候选（非故障注入）固件；完成必要的空白安全页本地初始化；串口发 `HEALTH?`；查看 `CTRL`、`WDG`、`RESET`、`LOCK`、`POLICY`；通过已有 `ADC?`, `ENV?` 或 OLED 状态读取一个既有非关键外设错误/未就绪情形，无需带电拔线。

**预期**：HealthTask 有进展时 `CTRL=RUN`, `WDG=ON`；普通 OLED/BME/ADC 问题仅模块局部失效，不能导致无意义重启或授权旧运动。真正远程超时/STOP 仍按 P3 原规则安全停机。Key0 与 Key1 原行为不变。

**证据**：1～3 行 `HEALTH?` 输出和必要的 `ENV?`/`ADC?`/OLED 观察。不要硬造错误场景。

## T2：实际 IWDG 重启（专用测试固件）

**安全条件**：断开独立舵机电源，确认 PC6/PC7 未连接负载，`P3B_OUTPUT_STAGE=0`，编译时专门启用静态注入宏；生产镜像严禁普通 CAN/UART 指令触发停喂。

**操作**：

1. 正常 HealthTask 条件喂狗运行，应能稳定读 `WDG=ON`。
2. 在无舵机输出测试固件中受控停止刷新 IWDG；观察 MCU 重新启动。
3. 重启后发送 `HEALTH?`，期望 `RESET=IWDG`，`LOCK=1`，`POLICY=LOCK`。
4. 在锁定状态发送普通 MODE=LOCAL/REMOTE、CONTROL/IMU READY 不得自动运动或授权；原 STOP 仍可处理。
5. NRST / 断电重上电之后仍 `LOCK=1`；通过两步 `SAFE CLEAR?` + `SAFE CLEAR YES` 恢复后应为 `POLICY=MANUAL`、`LOCK=0`、自动 LOCAL 禁止，只有新的有效 MODE 才可能运行。

**预期**：只有同时出现 IWDG reset flag 和新的启动行为才能把事件认作看门狗复位；串口突然无响应不构成证明。若无法读到 `RESET=IWDG`，标为 FAIL/待排查而不是 PASS。由于无实测 LSI，只估计约 1.33..2.67s 看门狗实际计时（加上停止喂狗后的剩余倒计时与启动耗时）。

**证据**：启动前/停止刷新后的简短串口结果，必要时逻辑分析仪观察 UART 启动或 PC6/PC7；无强制 trace GPIO。

## T3：短时间联合运行

**操作**：ControlTask、ImuTask、CanTask、UartTask、ADC DMA、SensorTask、DisplayTask、HealthTask 同时运行几分钟；UART 连续可正常查询（不要增加高频 HEALTH 输出），RK3588 SocketCAN 通信，OLED 翻页，ENV/ADC 持续更新。

**预期**：无意外 IWDG Reset，ControlTask 20ms 周期保护继续有效，Can/Uart/ADC/OLED/BME 功能维持，局部异常不会触发不必要停喂。优先无负载。P7 才进行系统并发压力测试。

## 精简记录模板（每组一张）

```
测试组: T1 / T2 / T3
固件版本/编译宏:
操作:
预期:
实际:
可观测证据（最多数行）:
结果: PASS / FAIL / NOT RUN
已知限制:
```

## 放行条件

- Keil 真实编译 PASS；测试 T1/T2/T3 各有真实结果 PASS。
- IWDG 已确认软件启动；停止喂狗确实发生 IWDG reset；复位后锁持续存在且不会未经人工操作自行恢复旧运动。
- P6a 绝对保留 P3 线上协议和 KEY0/KEY1；生产二进制没有 T2 停喂测试路径。
- 若测试异常，明确报告而不宣称 P6 ACCEPTED。
