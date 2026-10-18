# P6a Codex 可执行任务单

按顺序执行；每一项结束给出文件列表、关键不变量检查和最多 5 行结果摘要。

## P6a-0：预检（不写代码）

阅读 `references/source_audit.md` 和全部必查源码；验证 Git HEAD、Keil IROM、Flash/BKP 驱动、ControlTask 可授权出口、KEY0/KEY1 当前行为，列出与本文档冲突项。提交最多 10 条的最小修改计划；不能越过门禁自行重写 P1～P5。

## P6a-1：安全持久状态与引导

实现单页 16-bit 追加日志：AUTO/LOCK/MANUAL、INVALID、EMPTY fail-closed；读取 RCC_CSR flags，IWDG 优先；Flash 只在启动阶段使用。加入 `Health_BootCaptureReset`, `Health_BootFinalize`, 只读 snapshot/getter。FLASH 存储满/损坏均维持 LOCK；无自动页擦除。加入 Keil FLASH 驱动和 IROM 预留。

## P6a-2：控制授权门禁

在 P3bCycle 的 MODE/输出前和最终授权使能点加 Health gate；保留 KEY0 锁存、STOP、fatal 优先级、所有 P3 CAN 冻结字段。将 `boot_local_pending=true` 替换为策略 getter；检查 AUTO 与 MANUAL 两种情况。

## P6a-3：USART1 本地恢复

在现有唯一 UartTask 实现 `HEALTH?`、`SAFE CLEAR?` / `SAFE CLEAR YES`、10秒确认窗口；检查 Controller Disabled、Actuator Disabled、KEY0 未锁、PE4 未按。BKP 双半字请求 + `NVIC_SystemReset`，仅在 UART 发送完成后复位。故障 LOCK -> MANUAL，不允许故障后自动 LOCAL。

## P6a-4：编译交接

给出实际改动 diff、MAP/HEX 安全页地址检查、版本基线和短测试命令；无法在环境运行的 Keil 编译/烧录必须列 `NOT RUN`，等待用户提供实机报告。填写 `P6a_HANDOFF.md` 并等待 P6a ACCEPTED，再开始 P6b。
