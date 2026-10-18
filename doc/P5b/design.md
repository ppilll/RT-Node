# P5b 设计：双 I²C、多源显示与单键停止

## I²C1 与 OLED

- PB6→SSD1306 SCL、PB7→SDA、3.3V、GND；GPIO AF 开漏、100kHz、HAL I²C1 独立 Handle。OLED 通常 7 位 0x3C（厂家示例写字节 0x78），按设备 ACK 为准。
- 板上 24C02 也接 PB6/PB7，地址通常 0x50。**不操作 EEPROM**，不通过通用全地址写扫描，不清 EEPROM 内容；评估实际总线上拉与总线 BUSY 状态。
- 厂商 `oled.c/oled.h/oledfont.h` 示例针对 StdPeriph、GPIO 软件模拟 I²C：PG12 SCL/PD5 SDA/PD4 RES，不适用于四针模块的 HAL 硬件 I²C；`I2C_WaitAck` 未真实检查 ACK，`OLED_WR_Byte` 按字节事务、阻塞式延时不适合直接搬入。
- 可按许可审查后复用 128×32 SSD1306 初始化参数、6×8 字体、页缓存绘制思路；剔除无用的圆/线/动画/中文大字库。厂商源包含受限学习用途条款或来源限制，Codex 应记录，并优先选可合法使用的紧凑 ASCII 字库；不要直接提交其整套厂商示例/固件工程。
- 四针模块无独立 RESET 引脚；不得强驱动 PD4 或沿用 200ms GPIO 硬件复位。需用控制命令初始化并实机确认。
- `uint8_t fb[128*4]` 512B，按页（每 8 行）绘制；只需固定 ASCII 6×8（每行约 21 字符）。尽量一次性构图，短的页/块传输；I²C 数据控制字节 `0x40`，命令控制字节 `0x00`，分块发送控制字节必须随每条独立事务提供。
- 200ms 为建议刷新目标而非硬指标：100kHz 全帧仅纯数据 ~46ms+协议开销。OLED 传输须可被高优先级 ControlTask 及时抢占；分 8/16/32 字节块并在块间让出 CPU，或采用更长刷新周期（例如 300~500ms）以保证 ImuTask 10ms。不要把 4 页 512B 的循环包装在关 IRQ 区间。

## 4 页已冻结，字段布局为实用建议

1. **ENV**：温度 °C、湿度 %RH、气压 hPa（由 Pa 转换）、ENV OK/STALE/ERR；从 P5a EnvApp_GetSnapshot 读；不自行触发采样。
2. **IMU/PWM**：roll_deg、pitch_deg、LOCAL/REMOTE、已提交 `ActuatorSnapshot.applied_pan_us/applied_tilt_us`（屏幕空间不足可简写）；明确 PWM 软件目标不等于机械角度。
3. **CAN**：来自 CanTask 解析后的最近 ID、最近远程请求 Pan/Tilt（**接收值**，不是执行值）、最新通信年龄/包计数或运行模式；通过只读小快照提供，不读 FIFO。仅在已有 `Control_GetStatus` 不够时增补。
4. **UART**：UartTask 最近解析出的 ASCII 命令摘要（有界截断）、RX/OVF 计数、ADC 最新值或通信状态；从 UartTask 提供只读摘要，不读 DMA buffer。

页面示例布局并非实测值；实际每行不能超过显示宽度。任何无效值显示 `--`/`STALE` 而不是默认 0。每次页面切换应清除上一页缓存，避免叠字。

## KEY1：显示任务独占的翻页按键

- PE3 低有效，每 20ms 或类似周期轮询，稳定按下约 40ms 后只翻一次；必须释放后才能再次翻页，长按无连续翻页。初次开机按住应等待释放，再允许首个有效新按下。
- 不要将翻页放在 ControlTask；从原来的 `InitKeys` 和 `P3bCycle` 去除 KEY1 停机依赖；使用 DisplayTask 独立 GPIO 配置/读取（可设计一个简单共享 BoardKey Init 但不要求框架）。
- OLED 故障不能影响 KEY0 停机，显示任务不得持有控制关键资源。

## KEY0：LOCAL 自动启动 + 全局停止锁定

关键现状：`App/control/control.c` 的 `Motion()` 目前要求 IMU READY、KEY0 释放后连续按下以取得 LOCAL 授权；`P3bCycle()` 的 KEY1 按下两周期走 `Disarm()`；`Disarm()` 是**可重新授权**的正常状态，故简单删除 KEY0 条件会造成 STOP 后自动重启。

**实施必须体现：**

- ControlTask 在每个 20ms 循环尽早采样 PE4 低有效 KEY0，最小消抖可沿用 2 周期规则；一旦确认停止，**先禁用 PWM，随后**置内部锁存标志，永久禁止本次开机所有 `Enable()`/`Apply()` 和 LOCAL/REMOTE 重授权。锁存优先级高于并发 CAN MODE/REMOTE、晚到命令和普通状态迁移；若 KEY0 上电已按住，则没有任何初次 PWM 输出。
- 锁存标志不随 `Invalidate()`/`Disarm()`/模式切换重置；唯一自动清除方式是 MCU 冷/热复位重新初始化 RAM。建议通过 `Control_GetStatus` 增补只读 `key0_stop_latched` 或小 getter 供 OLED 展示，但不得改变 CAN STATUS 的已有 8 字节协议。
- LOCAL 无物理 KEY0 启动：只有在上电初始化、IMU snapshot 状态 READY、valid、新鲜、数值可用、KEY0 非按下且无锁存，才允许自动进入授权状态、捕获 `LocalFollow_Start` 相对参考角；保留 `CONTROL_PRIMING_TICKS`（40ms）、Slew、PWM 安全范围、ControlTask 周期及安全检查。
- **一次自动启动机会**：不允许每次回到 `P3B_DISABLED` 就自动启动 LOCAL。CAN STOP、CAN MODE=0、REMOTE 过期后保持现有停机/协议语义；合法的显式 CAN MODE 转换仍可经现有 epoch/action/TTL 检查激活对应模式（KEY0 未锁定时）。
- KEY0 锁存不可被普通 CAN STOP/新的 CAN MODE 覆盖。CAN STOP 本身仍是普通协议 Disarm，**不自动等同 KEY0 永久锁存**。不要删安全故障锁存、CAN ISR overflow/TTL 守卫、TIM3 安全门控。
- 软件停止 ≠ 断电硬件急停。只靠 MCU 软件复位会清除 KEY0 latch，复位后系统可再次自动动作；用户接受本设计，实机上电/复位观察仍为验收必须项。
- **实际输出授权**：`P3B_OUTPUT_STAGE` 基线默认 0，不能擅自设 2 或关闭 `P3B_LOADED_APPROVED` 等许可；T3 真正舵机输出测试须在用户明确确认已有授权固件/负载状态后进行。

## 故障限界

OLED I²C 错误→该页显示不更新/invalid，不影响 Control；短超时，不做复杂多阶段复位；遇到 STM32F1 ES0340 的 BUSY 卡锁才引入针对性有界处理。UI 不允许在串口或 CAN 上主动发命令以改变模式。
