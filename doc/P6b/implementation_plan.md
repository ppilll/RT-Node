# P6b 逐文件串行实施

## Gate 0：P6a 交接

- 获取 P6a 的实际提交 SHA、Keil 编译结果、UART/Flash BootSnapshot 验证；确认 no-auto-after-IWDG 仍由 P6a 主导。未通过不可启动 P6b。
- 核查新代码和 Keil 编译宏，确认 IROM 最后一页保护仍然有效。

## Gate 1：静态 HealthTask

1. 在 `App/health` 扩展 `health_app.c/.h`（或同目录小文件）；静态 TCB、静态栈、仅使用已启用的 `xTaskCreateStatic`。
2. 任务优先级初版0，周期 100ms；以 `Control_GetDiagnostics().wake_count` 快照变化证明 ControlTask 活性，300ms 停滞门限；区分 `INIT/RUN/STALLED`。
3. 不再给 ControlTask 添加第二个心跳；若需验证 `wake_count` 入口位置是否足够，可写在限制而非重做任务。
4. 不做 I²C/SPI/阻塞 UART、Flash 操作或 PWM 更新，不持有新的全局互斥锁。

## Gate 2：IWDG 唯一启动/刷新点

1. Keil `.uvprojx` 加入已有 `stm32f1xx_hal_iwdg.c`；`hal_conf.h` 目前已启用 HAL IWDG 宏，按实际头文件验证。
2. 仅 HealthTask 在调度器已 RUNNING、观察至少两次真实 ControlTask 进展之后调用 `HAL_IWDG_Init()`。`Prescaler=64, Reload=1249`，注意 HAL Init 内部已会启动硬件，返回 timeout 也可能不能再关闭。
3. 每次刷新前检查距上次 ControlTask 更新未超过容限；**全工程源码搜索 `HAL_IWDG_Refresh`, `__HAL_IWDG_RELOAD_COUNTER`**，生产代码实际喂狗点必须只有一个。
4. 设置 `WDG` 状态供 UART 查询，避免在 HAL_Init 失败但可能已经启动时误报 OFF。
5. 查看 STM32 Option Bytes 配置：若硬件启动 IWDG 无法确认与软件启动契合，则标注前置阻塞而非擅自假定。

## Gate 3：健康查询

- 在 P6a `HEALTH?` 返回基础上增加 `CTRL`、`WDG`，检查最坏回复不超过 64B buffer，继续由唯一 UartTask 发送，不刷屏；保留 `SAFE CLEAR` 命令、安全门和 BootSnapshot。
- OLED 只有空间充足才扩展已有页面。P3 CAN STATUS 绝不变化。

## Gate 4：测试编译分支

- 明确 `P6_WDG_TEST_ENABLE` 默认 0，只有 `P3B_OUTPUT_STAGE==0` 且其他本地显式无负载确认宏成立才允许启用。用户必须在实际接线确认独立舵机电源已断开。
- T2 模式可在首次成功启动 IWDG 并经过固定短时间后受控停止刷新；重启如读取 IWDG reset 标志则不再注入。
- 对于反复 IWDG reset 可能触发锁定不可读，优先保留 USART 读 BootSnapshot 的稳定时间；不能用没有回执的测试宣称 PASS。

## Gate 5：编译和实测交接

- 由用户在 Keil 实际编译并提供结果。对照 `doc/P6b/test_plan.md` 三组测试，记录确切 T1/T2/T3 操作和实际串口/CAN/OLED现象。
- Codex 填写 `P6b_HANDOFF.md` 与最终 `P06_HANDOFF.md` 的已知部分；Keil 实机缺失则维持 `NOT RUN`，不得提前 P6 ACCEPTED。
