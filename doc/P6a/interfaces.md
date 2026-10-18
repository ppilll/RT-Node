# P6a 接口契约（先设计，名称可小幅调整但语义不可改）

## `App/health/health_app.h`

```c
#ifndef RT_NODE_HEALTH_APP_H
#define RT_NODE_HEALTH_APP_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HEALTH_STORE_EMPTY_LOCKED = 0,
    HEALTH_STORE_AUTO,
    HEALTH_STORE_IWDG_LOCKED,
    HEALTH_STORE_MANUAL_ONLY,
    HEALTH_STORE_BAD_LOCKED
} HealthPersistentState;

typedef enum {
    HEALTH_RESET_UNKNOWN = 0,
    HEALTH_RESET_POWER,
    HEALTH_RESET_IWDG,
    HEALTH_RESET_SOFTWARE,
    HEALTH_RESET_PIN,
    HEALTH_RESET_OTHER
} HealthResetReason;

typedef struct {
    uint32_t reset_flags_raw;
    HealthResetReason reset_reason;
    HealthPersistentState persistent_state;
    bool authorization_allowed;
    bool auto_local_allowed;
    bool manual_recovery_boot;
} HealthBootSnapshot;

/* Direct register capture. Called once in main() immediately after
 * P1a_ForceSafeOutput(), before HAL_Init(); no RTOS/HAL blocking APIs. */
void Health_BootCaptureReset(void);

/* main() after HAL/timebase and clock ready, before static tasks start.
 * Performs any necessary FLASH halfword append and read-back.
 * Fail-closed; return false means startup safe lock can't be validated.
 */
bool Health_BootFinalize(void);

/* Read-only immutable boot data; ordinary task context. */
void Health_GetBootSnapshot(HealthBootSnapshot *out);

/* ControlTask only: gates mode authorization and automatic LOCAL. */
bool Health_ControlAuthorizationAllowed(void);
bool Health_BootAutoLocalAllowed(void);

/* UartTask only: creates one-shot BKP ticket after safety gate passes.
 * Does not itself unlock FLASH or modify PWM. Returns false on failure. */
bool Health_StageLocalRecovery(void);
/* TX/error path: invalidate staged request; no Flash/PWM operation. */
bool Health_CancelLocalRecovery(void);
#endif
```

`Health_StageLocalRecovery` 的调用者必须提前验证安全条件；或者在内部做一次尽可能轻量的二次验证。禁止把 `true` 解释为 Flash 已解锁，仅代表已经安排一个可在下次受控启动时检查的请求。

## UART 命令（原 UartTask 唯一 TX）

- `HEALTH?` -> `HEALTH RESET=<...> LOCK=<0|1> POLICY=<AUTO|MANUAL|LOCK|EMPTY|BAD>\r\n`（需确认 64B reply 内；必要时缩写，建议最终实际 ASCII 最大长度 <=63）。
- `SAFE CLEAR?`：仅锁定（包含 EMPTY/LOCK，不包含 BAD）+ ControlStatus.Disabled + Actuator Disabled + KEY0 未锁存且按键未按下 + 无 driver fatal 时，进入 `SAFE READY 10S\r\n`；不满足回复 `SAFE DENIED\r\n`。
- `SAFE CLEAR YES`：必须在 10 秒有效窗口内且二次确认安全条件，通过才调用 `Health_StageLocalRecovery()`，返回 `SAFE REBOOT\r\n`，**等待 SendReply 成功**再由 UartTask 受控系统复位；拒绝 `SAFE DENIED\r\n`。
- 恢复后的状态以新启动 `HEALTH?` 为准（可以回复 `SAFE UNLOCKED` 但仅基于实际 BootSnapshot）。

2026-10-18 实施格式：`HEALTH RESET=<UNKNOWN|POWER|IWDG|SOFT|PIN|OTHER> LOCK=<0|1> POLICY=<EMPTY|AUTO|LOCK|MANUAL|BAD> AUTO=<0|1> WDG=OFF\r\n`，最长 58 字节（含 CRLF，无需 NUL），原 buffer 64 字节。AUTO 字段表示当前启动的自动 LOCAL 机会，不代表当前控制模式。WDG=OFF 仅表示 P6a 软件没有启动 IWDG，未测 Option Bytes。该格式说明是预期接口，非实机回执。

确认窗口在任意其它完整命令、RX 错误、溢出/坏行或一次 YES 后取消；YES 必须在 `<10000 ticks` 内。正常 LF/CRLF 均支持，嵌入 CR 拒绝，避免拼接维护命令。TX 成功后再次检查安全条件才复位；失败或条件变化时调用 `Health_CancelLocalRecovery()`，不复位。`SAFE REBOOT` 始终只代表重启请求，Flash 最终结果必须用重启后的 HEALTH? 核实。

所有指令都要精确匹配（现有 `s_line` 最多 31 个可见字符），只由 UartTask 处理，不能引入第二发送线程。不要连续打印健康日志。

## State 与输出约束

- `persistent_state = AUTO` 且 `manual_recovery_boot=false`：`auto_local_allowed=true`；控制授权还需原 P5 规则。
- `persistent_state = MANUAL_ONLY`：`authorization_allowed=true`、`auto_local_allowed=false`，并在任何系统复位后仍然 false。
- `persistent_state = EMPTY/LOCKED/BAD`：二者全 false。
- `manual_recovery_boot=true`：本次重启 `auto_local_allowed=false`；绝不从人工清锁直接产生新运动。
- 已有 `Control_GetStatus` 的 P3 CAN 线上映射保持不变；持久锁信息仅通过 UART/可选已有 OLED 页面提供。
- `KEY0_STOP` != `HEALTH_LOCK`，不得共用/互相清除状态变量。

## 内部 Flash 记录助手（仅 health_app.c 内）

建议以 static private helper 分隔：`FlashScan`, `FlashAppend`, `IsLegalTransition`, `BkpTakeTicket`, `DecodeResetFlags`。**不要公开** `FlashErase`, `SetUnlocked`, `ClearKey0` 之类运行时旁路接口。记录魔数与预留地址只集中定义一次。
