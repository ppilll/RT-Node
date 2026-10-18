# P6b：HealthTask 与 STM32F103 IWDG 集成

**前置条件**：P6a 已完成代码审查、Keil 编译与本地恢复验证，用户确认 P6a 可作为 P6b 基线。P6b 必须重读 P6a 的实际 diff 和 `P6a_HANDOFF.md`，不得平行重写文件。

## P6b 输出

- 静态 HealthTask，监督 ControlTask 进展；独立 IWDG 仅由 HealthTask 条件刷新。
- 保存和输出 RCC_CSR 复位原因（P6a BootSnapshot 复用），整合健康状态到 `HEALTH?`。
- 标记清楚的 **无 PWM、无舵机电源** T2 测试固件受控停喂；默认生产镜像无此入口。
- `T1/T2/T3` 三组精简实际测试说明、Keil 编译报告、`P6b_HANDOFF.md` 和最终 `P06_HANDOFF.md`。

## 不做什么

不再做一整套 BME/OLED 恢复、所有任务强制心跳、额外 CAN 帧、独立 GPIO trace、大量 Flash 故障日志、P7 压测。
