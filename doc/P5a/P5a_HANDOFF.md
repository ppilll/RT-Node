# P5a_HANDOFF

2026-10-18：`CODE COMPLETE / KEIL BUILD OK / T1 NOT RUN / NOT ACCEPTED`。
本次填写实现与构建事实；用户执行 T1 并提供结果后，再更新下面的实机记录。

| 字段 | 实施记录 |
|---|---|
| checkout / branch | `git@github.com:ppilll/RT-Node.git`，`main`；执行前 fetch 后 HEAD 与 origin/main 均为 `4093ca3`（P4）。本轮变更未提交、未推送 |
| 已有工作区修改 | 保留 UART TX 超时 3→10ms 的既有修改；保留未提交 P5 规格及压缩包；没有回退用户文件 |
| 引脚与地址 | 硬件 I²C2，PB10/PB11 AF 开漏，100kHz；3.3V、CSB 高、SDO 低；代码 7 位 0x76、HAL 0xEC；实际 ACK/ID 尚未实测 |
| Bosch 来源 / 许可 | 官方 https://github.com/boschsensortec/BME280_SensorAPI ，固定提交 `c90d419492e26dd95586598a794e65eb2760753a`，源头标注 v3.5.1；BSD-3-Clause，三个源码/头文件及 LICENSE 原样保留于 `Driver/bme280/` |
| `.ino` 评审 | 已读 `Resources/BME280_I2C.ino`。仅参考，不移植其补偿公式：缺 ID/读长度/错误检查，H1/H3 有符号类型不匹配，H4/H5 符号扩展不严谨，Normal 启动后写 config 顺序不合适 |
| 任务 / 快照 | 静态 SensorTask，idle 优先级，512 words 栈，500ticks 周期；x1 T/P/H Forced，Bosch 9300us 转换等待以 RTOS 阻塞完成，状态清零且回到 sleep 后读完整数据；短临界区复制 EnvSnapshot |
| 失败行为 | 初始化失败保持 ENV invalid，等待 MCU 复位；采样失败保留最后成功数值与 tick、valid=false，下个常规周期继续尝试。无全局 FatalError、总线恢复框架或传感器串口输出 |
| 补偿模式 / 单位 | 全工程 `BME280_64BIT_ENABLE`；T 为 0.01°C 直接复制，H 为 1/1024 %RH，以 `(H*100+512)/1024` 转 0.01 %RH，P 为 0.01Pa，以 `(P+50)/100` 转 Pa；该版本 P 不是 Q24.8 |
| `ENV?` | `ENV:T=25.34C,H=50.12%,P=101325Pa,tick=12345\r\n`（格式示例，非实测）；未发布 `ENV NOT READY\r\n`；无效或成功样本年龄 >2000ticks 返回 `ENV ERROR\r\n` |
| Keil ARMCC5 构建 | 已实际执行，最终 0 errors / 0 warnings，退出码 0；生成 AXF/HEX，详情见下 |
| T1 实机 | **NOT RUN**：未收到用户真实地址、ID、串口或环境变化结果 |
| 后续依赖 | T1 未通过不标记 P5a ACCEPTED；未进入 P5b/P6 |

变更文件：`App/env/env_app.c/.h`、`Driver/bme280/bme280.c/.h/bme280_defs.h/LICENSE/README.md`、`Core/Src/main.c`、`App/uart/uart_app.c`、`Projects/RTnode.uvprojx`、本交接。Keil 注册 HAL I²C 源码、环境模块与 include/补偿宏；更新 Projects 既有构建产物。未创建大型证据目录。

没有修改 FreeRTOS 配置、HAL 源码、KEY0/KEY1、SPI1/IMU、CAN 协议或 TIM3/PWM 安全逻辑；`P3B_OUTPUT_STAGE` 仍为 0；未实现 I²C1/OLED。main 仅在 ADC 初始化后创建非关键环境任务，I²C 实际初始化在调度器启动后。

## HAL、边界与资源核对

- 仓库旧 F1 HAL 的 Mem_Read 三字节尾部存在关 IRQ 后轮询 BTF 路径。适配层读拆成最多 2 字节的 HAL 事务，避开该路径；HAL 1/2 字节必要的短寄存器操作仍会屏蔽 IRQ，不在其中等待总线。Forced 转换完成且 sleep 后寄存器稳定，分段仍是同次数据。
- 每个事务 HAL timeout=5ms；调用前以 IRQ 开启的最多 5ms BUSY 等待允许前个 STOP 完成，避开 HAL 固定 25ms BUSY 等待。无关中断长传输、GPIO 时钟恢复、I²C DMA 或 Mutex。实际总线时序仍需 T1 核对。
- 芯片 ID 必须为 0x60；读取全部温压湿校准值，设置后读回 x1/filter；拒绝 dig_p1=0 与跳过测量哨兵值。Bosch 补偿器本身会限幅，不能据此声称已验证物理精度或所有校准损坏。
- 补偿结果先检查 T=-4000..8500、H=0..102400、P=3000000..11000000，再换算。最大 H 乘加=10240512、P 加法=11000050，均在 uint32_t 内。
- 最大 ENV 行 `ENV:T=-40.00C,H=100.00%,P=110000Pa,tick=4294967295\r\n` 为 **52 字节**；错误行最多15字节。复用原64字节缓冲，无 printf、无终止符需求。UartTask 仍是唯一 TX owner，每次最多30字节，段间服务 ADC；PING/COUNT?/ADC? 路径保留。
- 最终镜像 Code=36574、RO=382、RW=216、ZI=12512 字节；静态 RAM=12728 字节，相对 P4 的10432增加2296字节。SensorTask 栈2048字节已计入。链接调用图存在函数指针不可追踪部分，不能代替实机栈余量测量（NOT RUN）。

## 实际构建

使用 `D:/MKD5.36/UV4/UV4.exe -b <checkout>/Projects/RTnode.uvprojx -t "Target 1" -o <checkout>/Projects/P5a_build.log -j0`。
首次全工程构建11秒；最后修正 BUSY/STOP 等待及 UART tick 字段后，增量构建3秒；均0错误/0警告。
`armcc --vsn` 和最终 `env_app.o` 的 fromelf 编译器记录均为 **ARM Compiler 5.06 update 7 (build 960), ArmCC**。
uVision 日志首行虽显示注册名 V6.16，实际使用 ARMCC/Bin，不能据此误判 ARMCLANG。
工程 C99 与 stdint/INT64_C/64位运算均已实际编译、链接。未跑主机补偿测试或实机测试，不把编译成功写成 T1 PASS。

## 用户 T1 回填（当前均 NOT RUN）

1. 烧录本轮 `Projects/RTnode.hex`，上电。逻辑分析仪接 PB10/PB11：记录 0x76 ACK、寄存器 0xD0→0x60、约100kHz；后续约500ms一组 Forced 采集，读取有多段重复 START/STOP，属于实现预期。
2. CH340 115200 8N1，发送 `ENV?\n`（也接受 CRLF）；提供几行实际 ENV 输出，间隔约500ms以上，检查 tick 更新。既有 ADC 周期报告仍会出现。
3. 轻微改变温湿环境，记录前后数值，确认气压合理；简单发送 PING、COUNT?、ADC?，记录原功能响应与其他任务观察。
4. 提供测试日期/所烧固件、实际地址/ID、串口输出、结论及异常。没有在线调试器，不要求额外全量复测。

| T1 记录 | 实际结果 |
|---|---|
| 日期 / 固件版本 | 待用户提供 |
| T1-1 地址 ACK / ID / SCL | NOT RUN |
| T1-2 实际 ENV 输出 | NOT RUN |
| T1-3 温湿变化 / tick / 气压 | NOT RUN |
| T1-4 PING / COUNT? / ADC? / 其他任务 | NOT RUN |
| T1 结论 | NOT RUN，未验收 |
