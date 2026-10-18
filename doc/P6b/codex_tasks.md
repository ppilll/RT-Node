# P6b Codex 可执行任务单

## P6b-0：P6a 放行确认（不改代码）

读取实际 `P6a_HANDOFF.md` 和 P6a 源码；审计 `Health_ControlAuthorizationAllowed`、安全页、USART1 手动恢复、P3 MODE/STOP/KEY0 的行为。检查 Keil 无冲突。

## P6b-1：HealthTask

实现静态 100ms HealthTask，低优先级、独立静态栈，仅 `Control_GetDiagnostics().wake_count` 作为主监督源，约 300ms 无进展停喂。不要要求所有外围模块 OK；运行中的终止 FAULT 仍喂狗。提供只读 RuntimeSnapshot。

## P6b-2：IWDG

启用已有 HAL IWDG 编译源，HealthTask 验证调度 + 至少两个有效 ControlTask 监督间隔后软件启用 Prescaler64/Reload1249，只有该任务刷新。检查启动错误后的硬件不可关闭可能性和 Option Bytes；不得把 HAL_Init 失败当成看门狗没启动。

## P6b-3：健康输出 + 异常核对

扩充 `HEALTH?`（可缩写保持 <=64 字节），不更改 CAN。查阅 `P1a_FatalError`, StackOverflow Hook 与 IWDG 一起的后果：关闭输出必须优先，不能靠等复位实现急停。避免 ISR 或关键控制循环无条件刷新。

## P6b-4：无负载 T2 注入镜像

编译时全局默认 OFF；启用必须显式无负载确认与 Stage0。仅首次启动的受控条件会停止喂狗，IWDG 重启后取消注入，能经 UART 读到 reset + lock；不得留下远程命令停喂接口。

## P6b-5：交接

报告实际代码 diff、HAL 配置、任务门限、Keil 编译状态、T1/T2/T3 记录空白模板。用户回传实机结果后整理 `P6b_HANDOFF.md` 和 `P06_HANDOFF.md`，不提前执行 P7。
