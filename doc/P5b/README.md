# P5b — SSD1306 四页显示 + KEY0/KEY1 控制策略调整

**前提：** P5a 已成功实现并完成 T1（或用户明确允许在其已能编译、正在测试状态下开发 P5b）。先阅读 `doc/P5a/P5a_HANDOFF.md`、当前源代码和实际接口，不按本文臆测不存在的快照。

**范围：** I²C1 PB6/PB7 驱动 SSD1306 四针 128×32（实际上机核对）；显示 ENV、IMU/PWM、CAN、UART 四页；KEY1 PE3 翻页；KEY0 PE4 停止 PWM 并锁定至 MCU 复位，LOCAL 上电自动授权一次。保留 CAN 协议，严格处理 KEY0 高优先级停机。

**禁止：** SPI1 重构、EEPROM 访问、在 ControlTask 中驱动显示、改变 CAN 线上帧、取消 PWM 输出许可编译门控、重做 P1-P4 全量验收。
