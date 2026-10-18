# P6a 逐文件实现计划（串行）

## Gate 0：源码和构建基线

1. 读取 `main.c`, `interrupts.c`, `control.c`, `control_policy.c`, `actuator.c`, `uart_app.c`, `adc_app.c`, `env_app.c`, `display_app.c`, `FreeRTOSConfig.h`, `stm32f1xx_hal_conf.h`, Keil `.uvprojx`, `HAL_FLASH` 版本。
2. 核实 `P3B_OUTPUT_STAGE` 实际生效值与 Keil 宏。不要把 `P3B_OUTPUT_STAGE` 从默认 0 改为非零。
3. 检查是否已有 BKP_DR1/DR2 占用、FLASH 尾部静态常量/其他固件使用；如果冲突暂停并报告。
4. 将 `IROM` 缩至 `0x08000000 + 0x7F800`，说明下载器的 mass erase 对持久数据的影响。检查最终 ELF/MAP/HEX 实际范围即可，不要求多套哈希。严禁编译器仍可能链接进安全页。

## Gate 1：安全页与启动引导

1. 在 `App/health` 新建 `health_app.c/.h`；引入 HAL FLASH（含当前仓库必要的 flash_ex 实现如果 HAL API 依赖），并在 Keil `.uvprojx` 加入文件、include path。
2. `Health_BootCaptureReset`：P1a 安全输出后的无依赖 RCC 原始标志快照。不要因 RCC flags 组合而选择不安全分支。
3. `Health_BootFinalize`：在已有 HAL SysTick 正常时，消费/清空 BKP 单次票据，扫描安全页，IWDG 标志最高优先，必要时仅向擦除半字追加 LOCK / AUTO / MANUAL，不运行页擦除；读回确认，发布不可变 BootSnapshot。任何异常 fail-closed。
4. 在 `main.c` 插入新启动顺序，不能更改 P1a 第一安全操作和共享 SysTick 交接行为；启动问题按现有 fatal / 安全诊断路径处理。
5. 保证本轮未集成 IWDG 时不会凭空报告 `WDG=ON`。

## Gate 2：ControlTask 门禁

1. `boot_local_pending = Health_BootAutoLocalAllowed()`；保证 `ControlTask` 一进入循环就遵守健康门禁。
2. `P3bCycle` 普通 MODE 处理前加拒绝门，保留 `session->terminal`、KEY0、STOP 的既有处理顺序和驱动级 `Actuator_ForceSafeOutput`。锁住时把 `action_accepted=false`，禁止切换到 WAIT_LOCAL/WAIT_REMOTE 或启动 PWM。
3. 如果已有函数组合可能跳过 `P3bCycle` 门禁，在 ControlTask 的唯一 `Enable` 点加相同只读前置门禁；Health 不增加 PWM 读写/使能权。
4. 确保 locking 不构造 `CONTROL_FAULT_*` 的新“致命故障”循环，允许 UART/CAN 观测。
5. 不改变 `Control_PostRemote`/`Control_PostMode` 的 CAN 字段定义、mailbox 大小或 STATUS 8 字节位置。

## Gate 3：UART 人工恢复

1. `ParseByte` 增加 `HEALTH?`, `SAFE CLEAR?`, `SAFE CLEAR YES` 分支；尽量复用单次 64B buffer、`SendReply`, `AppendDecimal`，不要引入复杂 printf/多任务 TX。
2. UartTask 自己管理 10 s 确认计时（32bit RTOS tick unsigned 差值），无动态分配；每次命令只允许一个状态窗口。拒绝条件见 `interfaces.md`。
3. 控制检查至少包含 `ControlStatusSnapshot.state==P3B_DISABLED`、`!key0_stop_latched`、PE4 高电平、ActuatorSnapshot `initialized && !enabled && !fault_latched`；不绕过已有 terminal fault。
4. 二次验证后 `Health_StageLocalRecovery` 在 BKP DR1/DR2 写入一次性票据及反码、读回验证；串口回复成功发送后触发软件复位。回复失败不得谎报已经清锁。
5. 手动恢复后的首次启动禁止 auto LOCAL；在故障 LOCK->MANUAL 之后则永久禁止 auto LOCAL，直到另行批准新策略。

## Gate 4：审查与交付

- 对照源码确认：无新 CAN 字段、无第二正常舵机写者、无 KEY0/KEY1 重映射、无 UART 第二 TX 任务、没有 RTOS 内 Flash 运行时擦除。
- 运行可用的语法/静态检查；由用户提供实际 Keil 编译。绝不假称已经编译/烧录。
- 提交 P6a diff（新旧函数和安全门点）、关键代码路径说明、`test_plan.md` 可操作步骤。
- 只有 P6a 通过用户审核才进入 P6b，不擅自并行写 `main.c`。
