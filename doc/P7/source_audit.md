# Source audit · 源码事实与复核项目

## 本地复核完成（2026-10-19）

HEAD与批准基线c3e9408013ae009e2b6cb49ac8d53f79d169e430一致。已重新审阅Control/Policy/LocalFollow/IMU fusion/CAN/UART/Health/main/OLED及现有测试。确认KEY0 PE4锁存STOP，KEY1 PE3翻页；P2b配置头注释及旧P2b/P3b设计叙述属历史语义，不可作为当前按键授权依据。正式Stage0预处理不存在Motion/Apply定义，宏、MAP、HEX检查PASS。未改Health/Actuator/Core/Driver/工程配置/控制健康门限；安全页布局和IWDG喂狗策略保持原样。实际变更、时间点及硬件边界见[P07_HANDOFF](P07_HANDOFF.md)。

**此表来自2026-10-19按 `c3e9408...` 读取的真实仓库源码。Codex 必须复核本地checkout，不能把本文代替新的源码审计。**

| 文件 / API | 核查的事实 | 必须保持的约束 |
|---|---|---|
| `App/control/control.c`, `control.h` | `ControlTask` 静态prio1、每20tick；`Control_GetDiagnostics()`用短临界区读；DWT入口与执行段计时；旧`late_count`在terminal状态后不能作为所有周期违例数 | 不改变原 `late_count`、故障/stop/安全策略、`xTaskDelayUntil`、2000us门限；P7独立计数包括terminal后样本 |
| `App/control/control_config.h` | `CONTROL_PERIOD_TICKS=20`, `CONTROL_EXECUTION_US=2000`, `CONTROL_MAX_LATE_TICKS=1`, Control栈256 words | 不改 |
| `App/control/local_follow.c`, `p2b_config.h` | roll→Pan，pitch→Tilt，零位，死区±2°、限角±15°、3us/°、1500±45us、2us/update | 算法只主机回归，不做测试驱动的重构 |
| `App/imu/src/imu.c`, `imu_fusion.c` | IMU 10tick，静止100样本标定、互补滤波，`Imu_GetSnapshot()`；没有绝对Yaw/空间轨迹规划 | 不更改SPI和滤波算法以制造更好数字 |
| `App/control/p3b_config.h` | `P3B_OUTPUT_STAGE`默认0；Stage0不编译实际`Motion()`输出路径 | Stage0不得被称为手势PWM实测 |
| `App/can/can_app.c` | CAN 500k配置、CAN ISR接收、静态Queue深度8、CanTask prio0；`s_display.rx_count++`在`ProcessFrame()`入口；`s_rx_overflow`为会被ControlTask消费的布尔事件 | 只能旁路增加事件**累计**，不可改变原安全溢出标志消费；不能把rx_count称为有效命令数 |
| `App/uart/uart_app.c` | DMA1 Ch5 256B Circular、IDLE/HT/TC、115200；`PING`、`COUNT?`、`HEALTH?`；自动 ADC 报告200tick；`char reply[64]`、`SendReply`30B分块；唯一TX | 不改变DMA/环处理。回复保持最长64B内；分段与自动报告交错测试 |
| `App/health/health_runtime.c` | Health 100tick prio0，仅监督ControlTask wake_count；IWDG64/1249，当前单处喂狗；stack HWM已维护 | 不加任务心跳体系、不更改停喂逻辑/300tick门限 |
| `App/health/health_app.c` | Boot锁状态保存在Flash末页`0x0807F800`；AUTO/MANUAL/LOCK/EMPTY/BAD；RCC reset flags | 不擦安全页，不改恢复策略；不“自动”清锁 |
| `App/display/display_app.c` | OLED IMU页有Roll/Pitch及Stage显示；KEY1(PE3)翻页 | KEY1不是STOP |
| `App/control/control.c` | KEY0(PE4)低有效：`Key0Stop()`发出STOP并锁存 | **KEY0不是启动键**；旧P2b/P3b指南与新代码冲突 |
| `doc/P3b/rk3588_test.py` | 含现有 SocketCAN STATUS/MODE/STOP 命令；`remote --end local`提示仍说“按KEY0启动” | 原脚本及操作文档要修正提示/注释，不改变线上协议 |
| `doc/P6b/evidence/production_stage0/build.json` | P6b旧正式Stage0 Code43694 RO1062 RW248 ZI16448；0 errors/0 warnings为旧构建记录 | P7正式镜像资源重新从MAP取数，不能复用旧值充当结果 |

## 特殊审计项

- 分清 **DWT两次入口间周期**、**ControlTask入口到既有elapsed点**、**RTOS tick `last_period_ticks`**、**TIM3 PWM周期**；不能互换。
- 当两个负载工况时长120s和600s不同时，最大值自然受样本数量影响；报告样本数与测试时长，不把不同长度下的极值差理解为优化百分比。
- `uxTaskGetStackHighWaterMark()`单位`StackType_t` words；按目标MCU实际字长换算，查询本身可能比较费时，保持原ControlTask调用位置稳定。
- `CAN s_rx_overflow`是危险信号，不允许在诊断getter里take/clear；新增`qfail_events`等只能是观察型字段，硬件FIFO overrun callback可能重复报告，不能称其为精确丢帧数。
- 上电/重新烧录时保持现有安全页；如果烧录软件有全片擦除流程，应停止并人工确认，不能凭代码构建回避此问题。
