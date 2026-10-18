# P6a 设计细则（冻结候选 V1.0）

## 1. 健康状态分类

1. **关键控制状态**：ControlTask 是否按周期运行、执行器致命故障、终止故障；不重建 ControlSession 和 Actuator 的原有逻辑。
2. **模式/命令状态**：LOCAL IMU 有效期、REMOTE 命令 TTL、STOP/CAN 溢出；由现有 ControlTask 处置，不一概复位。
3. **非关键模块状态**：ENV/OLED/ADC/UART 等；局部故障不触发全局 MCU 重启。

`GetStatus`、`GetDiagnostics`、IMU/ENV/ADC/CAN/UART 既有快照尽量复用；Display 的 task-local `healthy` 尚不能从外部读，若确有需求仅加一个原子 bool getter，不建立事件框架。不要把任何 `snapshot.valid` 一概当作模块健康。

## 2. Boot 判定与先后顺序

1. `main()` 第一项硬件操作保持 `P1a_ForceSafeOutput()`。
2. 紧接着 `Health_BootCaptureReset()` 复制 RCC_CSR 原始值，标记本次 `IWDGRSTF/PORRSTF/SFTRSTF/...`。不要先清 RCC flags，不要使用 HAL/RTOS API。
3. `SystemCoreClockUpdate/HAL_Init/SystemClock_Config` 按现有过程完成。此时已有 HAL SysTick 支撑 HAL Flash 超时。
4. `Health_BootFinalize()`：打开 PWR/BKP 访问（**禁止 BKP 备份域重置**），读取并随后清除一次性 BKP 票据；扫描 Flash 追加日志；若本次 IWDG，优先持久写 LOCK；若已确认的受控软件复位，按恢复规则追加 AUTO/MANUAL；异常一律锁定。读取校验成功才能将 `authorization_allowed` 发布为 true。尽可能在这一步之后清 RCC reset flags，避免后续复位标志残留。
5. 后续 `Actuator_InitSafe()`、任务初始化、调度器启动与原仓库保持基本一致。绝对不能因为无法写 Flash 就进入“允许自动 LOCAL”的默认分支。

注：如果 `Health_BootFinalize` 无法把持久状态处理为有效可用值，可以继续以 `LOCKED/STORE_BAD` 运行低风险诊断路径（UART/其它模块），但绝不能放行 PWM 或输出授权。若代码结构做不到可观测锁定，退回已有 `P1a_FatalError`，需记录此选择的可观测性代价。

## 3. Flash 追加式单页模型（不用自动页擦除）

高密度 Flash：512KiB；末页起始 `0x0807F800`，终点 `0x0807FFFF`。Keil IROM 由 Size 0x80000 缩到 0x7F800。该页不包含任何代码、向量、只读常量或 HEX 数据。Codex 必须确认 Keil 生成的 HEX 不会覆盖该页；使用 CH340 下载时确认下载器的擦除策略：**如果每次下载 mass erase，需重新人工初始化**。

每条记录为一个新的 16-bit 半字：

| 编码 | 语义 |
|---|---|
| `0xFFFF` | 空槽，尚未写入 |
| `0x0F0F` | `AUTO`：常规 P5 启动策略（仅无故障初始化） |
| `0x3333` | `LOCK`：IWDG 已被识别并持久锁定 |
| `0x5555` | `MANUAL`：IWDG 后经人工恢复，但永久禁止自动 LOCAL |
| 任何其他值 | `STORAGE_BAD`，fail-closed |

三种有效值每个含 8 个清零位，从擦除态写入某一种时，编程中间结果不应恰好等于另一种有效值。仍要求 HAL Program 返回成功、半字读回等于目标；错误/电源掉落视为可能需要维修。

扫描规则：从首半字起顺序读取；遇 `0xFFFF` 则认为后续均应为 `0xFFFF`（若出现空洞，标记损坏）；任何未知值、非法状态迁移、满页均保守禁用。只有 `AUTO`、`MANUAL` 的有效**最后一条**记录可解除锁；存在 `LOCK` 后的任何自动重启，不会追加 AUTO。合法转换为：`EMPTY -> AUTO`（人工初始化）、`EMPTY -> LOCK`（首次初始化前已检测到 IWDG）、`AUTO -> LOCK`、`MANUAL -> LOCK`、`LOCK -> MANUAL`。重复 IWDG 且末条已 LOCK 时不再次写入。保证恢复次数受日志容量限制，可记录有限次数，不做运行时磨损管理；满页需离线维护，默认锁定。

2026-10-18 实施修订理由：原候选转换表遗漏 EMPTY 启动检测到 IWDG 的分支；若继续只保留 EMPTY，之后将可能经首次初始化写 AUTO，丢失“已发生故障后只能 MANUAL”的语义。增加 EMPTY->LOCK 与既定 IWDG 优先策略一致。锁写入失败时保留 RCC reset flags 以供后续 NRST/软件复位重试；不声称覆盖完全掉电丢失证据窗口。

对于状态异常，不能仅靠追加一个 MANUAL 就跳过未知值。操作员需离线重新刷机、擦除专用页，再做本地人工初始化；明确这是例外维修路径。

## 4. IWDG 优先于恢复票据

临时恢复票据只作为一次受控 software reset 的凭据，不是解锁证明：

- UartTask 在用户完成两步确认且安全条件满足时，写入两个 BKP 数据寄存器（例如 `BKP_DR1=TOKEN`, `BKP_DR2=~TOKEN`）；用已有 PWR/BKP 时钟/写权限接口；必须先检查仓库和板级程序没有使用同一寄存器。
- 返回 `SAFE REBOOT`（实际 TX 成功后），由 **UartTask** 调用 `NVIC_SystemReset()`。不得在 ParseByte 内立即复位丢失回执。
- 下次 BootCapture 保存 RCC flags 后，BootFinalize 先读取+清除 BKP token，再判断：仅 `SFTRSTF=1` 且无 IWDG、无 POR/PDR、无 WWDG（必要时进一步排除歧义 flags）时才接受；否则丢弃并保持锁定。
- `IWDGRSTF=1` 则始终优先记录 LOCK；票据永远不能绕过 watchdog 故障。
- 票据完成消费后必须清除并读回；如果无法清票，则不得应用恢复事件。
- Flash 记录完成并经验证，才能在 BootSnapshot 中发布 `manual_recovery_boot=true`；这一次启动禁自动 LOCAL（无论本次追加的是 AUTO 还是 MANUAL）。

BKP 的非易失保留依赖 VBAT，但这里丢失票据的结果是无法恢复而非误恢复；丢失时 fail-closed。

## 5. 控制安全门禁

当前 `control.c` 仅靠 `boot_local_pending=true` 即可在 IMU READY 时自动进入 LOCAL；同时 P3bCycle 的 MODE 分支可独立创建新的 LOCAL/REMOTE 会话。因此需要两处门禁：

- 初始化：`boot_local_pending = Health_BootAutoLocalAllowed()`，只在健康模块已验证 `AUTO`、非当前人工恢复启动、未持久锁定时 true。
- `P3bCycle`: 在处理正常 MODE/输出前必须检查 `Health_ControlAuthorizationAllowed()`；锁定状态下强制保持/发布 Disabled（原已有致命 Fault 优先，STOP/KEY0 仍然要处理），拒绝任何正常 MODE 使能；不得调用 `Actuator_Enable`、`Actuator_Apply`。
- 可在 `Enable()` 或最接近 Actuator_Enable 的控制出口额外加入最终只读门禁以便不受未来修改影响；只允许 ControlTask 执行实际输出，不让 HealthTask 写 PWM。
- 不更新或复用故障前 mailbox/epoch/generation，重新启动是新的 RAM 状态；人工恢复在重启边界后只能接收符合原规则的**新** MODE/CONTROL。
- KEY0 是否锁存与 P6 FLASH 控制锁是不同原因，不创建“解开 Health 便解除 KEY0”通路。

## 6. 对功能的影响

- 第一次刷 P6a 到空白安全页将显示 `UNPROVISIONED_LOCKED`；不能自动 LOCAL，属于有意的更保守行为。
- 人工初次初始化 `EMPTY -> AUTO` 后，当前恢复引导启动禁止自动 LOCAL；以后普通未发生 IWDG 的冷启动恢复 P5 原启动机会。
- IWDG 锁后 `LOCK -> MANUAL`，**此后所有重启都不自动 LOCAL**，只有新且有效的 CAN MODE 可授权（包括 LOCAL 模式）。这是保证跨多次复位不会旧运动自动重启的最小持久策略。
- 该 UART 流程假设 CH340 是现场维护通道；没有物理不可伪造性，不能宣称安全认证。

## 7. 不可回避的边界

- IWDG 触发后到 BootFinalize 实际写入 LOCK 前，若 MCU 再遭完全断电且 RCC 复位证据丢失，则可能遗留旧 AUTO 有效记录。纯片内软件无法达到“任意供电时序均不可丢失故障痕迹”的绝对保证。只声明**已成功持久化的锁跨断电保持**；若项目必须覆盖全部窗口，需要独立硬件锁存，或牺牲原 P5 自动 LOCAL、所有上电均人工授权。
- Flash 非抗恶意篡改与写保护方案；维修、mass erase、篡改、调试器更换镜像不在 P6 软件保证内。mass erase 后因 EMPTY fail-closed。
