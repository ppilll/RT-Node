# P6b 新接口与调用约束

在 P6a `health_app.h` 基础上小幅增加，不改 P6a 已经发布的 BootSnapshot 语义。具体函数命名以 Codex 实际 diff 为准，维护最少接口。

```c
typedef enum {
    HEALTH_CTRL_INIT = 0,
    HEALTH_CTRL_RUNNING,
    HEALTH_CTRL_STALLED
} HealthControlProgress;

typedef enum {
    HEALTH_WDG_OFF = 0,
    HEALTH_WDG_RUNNING,
    HEALTH_WDG_START_ERROR,
    HEALTH_WDG_STARTING
} HealthWatchdogState;

typedef struct {
    HealthControlProgress control_progress;
    HealthWatchdogState watchdog;
    uint32_t last_control_wake;
    uint32_t last_progress_tick;
    uint32_t min_stack_words;
    bool test_stop_active;
} HealthRuntimeSnapshot;

/* Created before scheduler starts; static task; no I/O. */
bool Health_InitTask(void);

/* Task-context read-only data to UartTask / optional DisplayTask. */
void Health_GetRuntimeSnapshot(HealthRuntimeSnapshot *out);
```

## 唯一调用关系

- `main()`: P6a BootCapture + BootFinalize -> `Health_InitTask()` -> 启动 scheduler；不直接调用 `HAL_IWDG_Init()`、`HAL_IWDG_Refresh()`。
- `HealthTask`: 唯一生产代码位置调用 `HAL_IWDG_Init()` 与 `HAL_IWDG_Refresh()`；必须以 ControlTask 进展为刷新条件。
- `UartTask`: 查询 `Health_GetBootSnapshot` 和 `Health_GetRuntimeSnapshot`，只构造短字符串并自己发送；绝不调用 IWDG Refresh。
- `ControlTask`: 不增加额外重复 heartbeat，`wake_count` 由已有代码维护；不在 ControlTask 尾部刷新 IWDG。
- `SysTick_Handler`、TIM3 IRQ、其它高频中断：禁止 IWDG 相关调用。

## 中断/任务语境

- `Control_GetDiagnostics` 在 task context 获取完整快照时使用临界段，不应从 ISR 调用。
- HealthTask `xTaskDelayUntil` 优先使用仓库已经启用的 API；不要依赖未开启的软件 timer task。
- 共享的 snapshot 采用短临界段保护跨任务读写；不能在临界段内调用 Flash/HAL IWDG 初始化/HAL UART 等阻塞操作。
- `wake_count` 回绕按照 uint32 无符号处理。

## 喂狗条件精确化

- 初次观察记录基线，不将第一帧仅作进展成功。
- 已读取至少两次 `wake_count` 增长（不同监督间隔）后可启动 IWDG，避免在只执行过一次控制入口的系统启动狗。
- 启动后每个 100ms 周期：如果距上一次 `wake_count` 有变化的时间不超过候选 300ms，且本次任务正常运行、内核处于 RUNNING，则允许刷新；超过门限停止刷新直到 IWDG 复位，除非经过人工批准更复杂恢复机制。
- 不使用 ControlTask 控制模式、IMU/OLED/ADC/ENV/UART 业务 OK 作为强制条件。

## 2026-10-18 实施补充

- 实际运行部分位于独立 `App/health/health_runtime.c`；P6a `health_app.c` 启动/Flash/BKP流程未改。
- USART1 仍接收 `HEALTH?`，实际输出为 `HEALTH C=<INIT|RUN|STALL> W=<OFF|START|ON|STARTERR> R=<UNKNOWN|POWER|IWDG|SOFT|PIN|OTHER> L=<0|1> P=<EMPTY|AUTO|LOCK|MANUAL|BAD> A=<0|1> T=<0|1>\r\n`。最大58B含CRLF，原64B buffer及30B发送块保留。
- C=ControlTask监督状态；W=IWDG初始化/硬件运行状态，**不表示本轮一定喂狗**；R/L/P/A复用P6a的复位/锁/策略/本次自动LOCAL机会。T=1只可能来自编译期T2镜像的已锁存停喂，生产恒0。
- START 在调用 HAL Init 之前发布，避免 UART 在 HAL 等待期间误报 OFF；失败发布 STARTERR，可能已启动硬件，之后不再调用 Init/Refresh。
- 首次采样只建基线；两次连续计数变化且采样间隔至少100ticks才可启动。无变化会清空启动计数。监督间隔或距最后进展严格大于300ticks时永久STALL，不因后续计数变化恢复；等于300ticks仍可喂狗，100ms采样下通常在400ms无进展处首次判停。
- 先判监督空档再接受新计数，防止 HealthTask 延迟后把空档掩盖为新进展。这是保守的监督策略，优先级0任务被持续饥饿也可能触发复位，留待T3实测。
