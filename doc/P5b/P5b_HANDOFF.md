# P5b_HANDOFF

2026-10-18：`CODE COMPLETE / KEIL BUILD OK / HOST CHECKS PASS / T2-T3 NOT RUN / NOT ACCEPTED`。
用户本轮明确暂不进行实机测试，继续代码生成；这不阻塞实现，不构成P5验收。

| 字段 | 实施记录 |
|---|---|
| 基线 / 工作区 | `main @4093ca3`加已有未提交P5a与本轮P5b；未提交、未推送。保留原UART TX timeout=10ms与用户资料；根目录没有README.md，已读P5文档包README |
| P5a依赖 | 已读原交接和真实源码；EnvApp_GetSnapshot完整复制T=0.01°C、H=0.01%RH、P=Pa、sample_tick、valid。用户确认P5a已完成测试，旧交接未更新；本轮不复测、不虚构地址/ID/输出 |
| I²C1 | HAL I²C1，PB6/PB7 AF_OD、外部上拉、100kHz；代码7位0x3C / HAL 0x78、128×32；实际ACK/尺寸NOT RUN。只发OLED固定地址，不扫描、不访问共线24C02 |
| OLED来源 | 已读Resources例程oled.c/oled.h/oledfont.h及USER/main.c许可。采用SSD1306的32行参数和紧凑字体，没有移植StdPeriph、软件I²C、PG12/PD5/PD4、硬复位、动画/中文字库 |
| 字库许可 | 中景园电子USER/main.c：学习用途，其他用途须作者许可，Copyright(C) 中景园电子2014/3/16 / All rights reserved。display_font.h保留来源/限制，提取space..z共91个6×8字形（546B），范围外显示`?`；不声称开放或额外商业/分发许可 |
| 任务 / 刷新 | 静态DisplayTask，idle优先级、512 words栈、512B framebuffer；首屏ENV。500ticks构图间隔，16B数据块，每5ticks步进至多一块，块前显式设置页/列，每条独立事务带0x00命令或0x40数据控制字节 |
| I²C故障 | 每条HAL事务5ms超时，另有最多5ms可抢占BUSY预等，全部TX轮询在IRQ开启时进行。上电等100ticks，先写整帧再Display ON；失败后停止I²C，仍轮询KEY1，复位后才重试。无全局FatalError、总线恢复或显示串口输出 |
| MSP | 新增唯一强定义Core/Src/i2c_msp.c::HAL_I2C_MspInit，按I²C1/I²C2分派。P5a原I²C2 GPIO/时钟设置迁入，SensorTask仍独占I²C2；采样/补偿/单位逻辑不变 |
| KEY1 PE3 | 仅DisplayTask配置/读取，5ticks轮询，按下/释放均需稳定40ticks，每次按下一页；初次按住必须先稳定释放，长按不连翻。ControlTask完全移除KEY1旧停止职责 |
| KEY0 PE4 | ControlTask每周期优先读取，首次低电平立即Disable，随后锁存，不等待消抖/释放；Enable/Apply前再检查。名义采样周期20ms，实际响应NOT RUN。开机按住直接锁定；松开、Invalidate/Disarm、CAN STOP/MODE/CONTROL不清锁，只有MCU复位清RAM |
| 一次自动LOCAL | 仅boot_local_pending且IMU READY/valid/年龄<40ticks/有限值、KEY0释放未锁时捕获LocalFollow参考并授权。首次启动或任何普通Disarm消耗开机机会；有效MODE（含已DISABLED时MODE=0）也取消开机机会。不是每次DISABLED自动重启 |
| 原控制保护 | 保留40ticks双时钟priming、限幅、Slew、epoch/action/seq/边界/TTL、CAN ISR overflow及致命fault。无KEY0锁定时普通STOP后仍可经有效CAN MODE显式重授权LOCAL/REMOTE；LOCAL不再等KEY0启动边沿 |
| 输出门控 | p3b_config.h未修改，默认P3B_OUTPUT_STAGE=0、P3B_LOADED_APPROVED=0。最终HEX为Stage0，Motion输出路径未编入，不能验证真实PWM/自动运行/priming或机械运动；未创建或烧录Stage2固件 |
| CAN线上兼容 | ID、DLC、8字节STATUS布局与解析规则不变，新增锁存布尔值仅用于本地快照。reason=0x01数值沿用物理按键停止，来源KEY1改为KEY0；不能只凭reason证明锁存 |
| 实机 | T2/T3、串口/CAN/ADC联合冒烟、栈余量、实际时序及OLED电气均NOT RUN；用户暂缓。未提议P5 ACCEPTED，未进入P6 |

## 页面与完整快照

每次构图先清512B缓存，每行最多21字符。每个来源各自完整复制，不宣称不同来源同时采样。

| 页 | 显示 / 来源 |
|---|---|
| ENV | WAIT/OK/ERR/STALE、T/H、P(hPa，Pa按0.01hPa格式化)、样本年龄；直接EnvApp_GetSnapshot，无效或年龄>2000ticks不显示旧测量值 |
| IMU/PWM | LOCAL/REMOTE/DISABLED/FAULT或KEY0 LOCK、R/P、PAN/TILT软件已提交目标、PWM ON/OFF和Stage；Imu_GetSnapshot、Control_GetStatus、Actuator_GetSnapshot。IMU非READY/valid/新鲜或数值不可用时`--`；OFF时中心目标不代表输出或机械角度 |
| CAN | 最近CanTask出队进入ProcessFrame的ID（包括被拒格式/内容）、RX数、年龄；最近STD/DATA/DLC8 0x180的原始请求Pan/Tilt，年龄>=100ticks标OLD。不是Control接受/执行值，无请求则`--/--` |
| UART | RX、最近完整ASCII解析行前15字符、OVF、ADC估计mV；命令年龄>2000ticks前缀OLD，无命令/ADC则`--`。保留COUNT?语义与UartTask唯一USART1 TX |

新增CanApp_GetDisplaySnapshot/UartApp_GetDisplaySnapshot，均短临界区复制完整结构。生产点为ProcessFrame、完整ASCII行解析及常规轮询；OLED不读FIFO/DMA环形区/PWM寄存器。ADC仍由UartTask独占Process，仅最终发布及Getter补短临界区，避免第二个读取任务看到部分更新。SPI1/IMU、CAN线上格式、UART DMA、ADC触发机制未改。

## 构建与代码验证

- 实际运行 `D:/MKD5.36/UV4/UV4.exe -b <checkout>/Projects/RTnode.uvprojx -t "Target 1" -o <checkout>/Projects/P5b_build.log -j0`；最终0错误/0警告，生成既有位置AXF/HEX，最终增量构建2秒。首次编译发现字体文件尚未落盘，补齐后通过。
- armcc --vsn实际为ARM Compiler 5.06 update 7 (build960)。日志注册名称V6.16不代表ARMCLANG，实际使用ARMCC/Bin。
- 最终Code=39774、RO=954、RW=232、ZI=15272字节，静态RAM=15504B，相对P5a12728增加2776B，包含DisplayTask2048B栈/512B缓存。链接调用图DisplayTask最大深度184B、ControlTask200B，有不可追踪函数等限制，不代替实机高水位/时序测试。
- 默认Stage0构建不覆盖Motion。额外以相同ARMCC/目标include，对输出分支做仅编译到临时.o的检查（Stage1条件），0错误；不链接HEX、不加载、不改工程许可。Stage2未启用。
- 已实际运行 `doc/P5b/tests/run_host_tests.ps1`，MSVC执行真实control.c/control_policy.c/local_follow.c/display_app.c与模拟HAL/RTOS，Stage0和模拟Stage1断言均PASS。覆盖KEY0按住启动/运行停止/松开不恢复/后续MODE+CONTROL拒绝/Disarm及Invalidate不清锁/复位资格，IMU无效/NaN/TTL边界，40ms priming，普通STOP/MODE=0/REMOTE超时/overflow不自动重启，显式CAN重授权、REMOTE slew、致命fault；KEY1启动按住/释放/短按/长按，显示无效/STALE/负温/单位/最大计数/行宽、16B传输及失败返回。
- 主机测试不运行真实调度器/IRQ/外设，不是T2/T3 PASS。源文件差异空白检查通过；Keil生成的dep/build_log自带格式空白，不为此改写生成物。没有新增大型证据目录。

变更文件：App/display/display_app.c/.h/display_font.h、Core/Src/i2c_msp.c、App/control/control.c/.h、App/can/can_app.c/.h、App/uart/uart_app.c/.h、App/adc/adc_app.c、App/env/env_app.c（仅MSP迁移）、Core/Src/main.c、Projects/RTnode.uvprojx及既有构建产物、doc/P5b/tests/、P5b接口/测试/交接与doc/P05_HANDOFF.md。

## T2/T3 实机回填

| 项目 | 实际结果 |
|---|---|
| 测试日期 / 所烧固件 / Stage / 负载许可 | NOT RUN；用户暂缓 |
| T2-1 0x3C ACK、100kHz、128×32、0x50无访问 | NOT RUN |
| T2-2 KEY1四页循环、单击/长按/启动按住 | NOT RUN |
| T2-3 ENV与ENV?对照、有效性 | NOT RUN |
| T2-4 IMU/PWM快照、OFF/Stage | NOT RUN |
| T2-5 CAN真实ID/请求/计数 | NOT RUN |
| T2-6 UART PING/COUNT?/ADC?/ENV?、摘要/ADC | NOT RUN |
| T3 联合短时运行 / 控制20ms、IMU10ms / 刷新 | NOT RUN |
| T3-A 上电按住KEY0，松开不恢复 | NOT RUN |
| T3-B 新鲜IMU一次自动启动/参考/40ms保护 | NOT RUN |
| T3-C 运行中KEY0停止，松开不恢复 | NOT RUN |
| T3-D 锁后新有效CAN MODE+CONTROL不能恢复 | NOT RUN |
| T3-E 复位后重新满足条件才启动 | NOT RUN |
| T3-F 普通CAN STOP不自动重启，未锁时有效MODE可恢复 | NOT RUN |

后续仅收集核心实测及已有输出许可，不重做P1–P4、不进入P6。全部适用项实际通过后才提议P5 ACCEPTED。
