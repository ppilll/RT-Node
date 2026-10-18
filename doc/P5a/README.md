# P5a — BME280 I²C2 环境采集

**唯一目标：** 在 PB10/PB11 使用 HAL I²C2 读取 BME280 三项环境数据，发布任务安全快照，并由现有 UartTask 回答 `ENV?`。

严格按 `design.md` → `interfaces.md` → `codex_tasks.md` → `test_plan.md` 执行。必须先审查 `vendor_inputs/BME280_I2C.ino`，再从 Bosch SensorAPI 的有许可版本选择补偿算法；不要新写未经核对的补偿公式。

P5a **不做** OLED、不配置 I²C1、不改 KEY0/KEY1、不动 SPI1、不改 PWM 授权、不改 CAN 协议。不因 sensor failure 调用全局 FatalError。通过 T1 后形成 `P5a_HANDOFF.md`，再进入 P5b。
