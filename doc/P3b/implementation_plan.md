# P3b Codex 实施计划（单会话、单一文件写者）

## 步骤1：版本与现状确认

- 先读适用AGENTS.md、`doc/P3b/*`，再读 `doc/P3a/interfaces.md` / `P3a_HANDOFF.md` / `doc/P2b`设计与接口；检查Git工作区、HEAD、未提交修改与基线 `b2c1f34595ae125a3b27f2d5143c4202762a705c` 的实际差异。若已有P3b修改，只基于新代码合并，避免覆盖。
- 确认P3a source `App/can/can_app.c` 内 `ProcessFrame`、`MakeStatus`、RX queue、CAN IRQ、`CanApp_Init()`；确认ControlSession一次性terminal语义、`control.c` 本地 `#elif P2B_OUTPUT_STAGE`、Keil路径。将用户报告的P3a双端T1通过记录为 `USER_REPORTED_PASS`，原始T1证据未提供则不伪造。

## 步骤2：先落地控制状态/双向邮箱（尚不启用输出）

- 定义最小`ControlRuntimeMode`(线上state0..7)、`epoch`/边界tick、动作结果快照；用已有RTOS静态内存和短临界区提供CAN↔CONTROL完整复制接口。CAN→Ctrl三个逻辑信息：最新remote target、单pending模式动作、单sticky STOP（另有overflow）；Ctrl→CAN完整STATUS控制状态。
- 不另建多条消息队列/重复数据通路。CanTask纯发布、不做最终模式许可；ControlTask才拥有epoch、源、使能状态。
- 先用编译和人工代码审查确认帧仍与P3a接口完全兼容。

## 步骤3：ControlSession正常重新授权（不动fatal）

- 在 `control_policy.[ch]` 新增源REMOTE=3、最小正常Disarm与Remote Authorize入口。保留TEST=1/LOCAL=2、TTL和generation校验、`ControlSession_Close` terminal/原fatal驱动latch。
- 正常STOP、模式切换、remote过期必须先Actuator_Disable再 DisarmNormal，不能调会锁存的ForceSafeOutput；存在真fault时不得重授权。禁止`memset(session)`和清Actuator内部锁存。

## 步骤4：ControlTask接管正式状态及执行器

- `control.c` 在原20ms任务中将新的正式LOCAL/REMOTE模式仲裁嵌入，隔离P1/P2旧试验分支；原KEY0/KEY1读取、READY/40ms样本TTL、LocalFollow映射、PRIMING>=40ms及DWT/2ms预算不丢。
- KEY0本地离线授权；KEY1全部模式STOP；GLOBAL STOP优先处理并形成禁用边界。切换或STOP时全部旧快照失效、epoch更新，同周期不允许从STOP再Enable。REMOTE首帧中心启动，随后每周期±2µs，最终Actuator限幅。
- CAN目标 `received_tick` age>=100 或其他来源异常按design区别normal timeout与fatal；相同generation不能反复交给Policy。

## 步骤5：将P3a协议处理真正接入Ctrl

- 在 `can_app.c` 用共享邮箱替换旧的MODE恒拒/STOP空操作/CONTROL仅忽略；保持其CAN硬件、filter、bit timing、ISR与queue。MODE_REQUEST处理等待Ctrl最终结果，STOP即安全事件，HEARTBEAT沿用原功能；Ctrl真实状态驱动STATUS。注意MODE同ID/同payload与旧action的拒绝/缓存语义。
- RX overflow P3b触发安全禁用；BUS-OFF/丢心跳不得无端停止LOCAL；STATUS仍非阻塞100ms为基础。

## 步骤6：输出阶段与Keil构建

- 增加最小 `p3b_config.h` 用于输出阶段0/1/2与许可，默认0。协调P2b原输出宏，禁止不合法叠加；确保用户可单独构建P3b Stage1无负载镜像，但不自动同意Stage2。
- Keil uvprojx只增加实际新.c及头路径；无需重新添加已有`stm32f1xx_hal_can.c`/`queue.c`；保持ARMCC5.06u7、72MHz、静态内存、没有重复强IRQ。
- 实际执行完整Keil构建，按真实结果报告0 errors/warnings或列出问题；无环境就标NOT_RUN。别用编译替代真实WCET。

## 步骤7：一套RK3588最小测试发送程序和T2-T4准备

- 首选单文件Python3 SocketCAN raw socket或极简C工具，允许按MODE→等待0x280 epoch确认→每20ms发递增seq目标→STOP→STATUS验证；如`cansend`就够，则不要新加完整框架。测试工具不应执行舵机危险大幅摆动，先中心和±10µs。
- 给出明确的P3b Stage1 HEX选择方法与LA(PC6/PC7/GND)观察步骤；T2/T3/T4按test_plan操作。Stage2只做用户批准后的有界测试，不由Codex声称已做。

## 步骤8：总结与停点

- 输出简洁`git diff --stat`及关键diff、Keil真实构建、少量测试事实、冻结接口变更检查、存在问题、`P3b_HANDOFF.md`（用模板填）。必要时指向已有P3a证据；不要拷贝旧目录，别反复算源码/HEX SHA256。
- 当T2/T3/T4未实机完成，状态写`BUILD_PASS / AWAITING_OPERATOR_TEST`；完整带载、源码、构建均实测后才能建议`P3b ACCEPTED`。**不得自动进入P4**。

## 变更文件责任（同一个Codex对话独占）

| 文件 | 责任 |
|---|---|
| `App/can/can_app.c/.h` | 修改协议业务、跨任务邮箱/STATUS；尽量保持CAN1底层初始化和IRQ不变 |
| `App/control/control.c/.h` | 仲裁、LOCAL复用、REMOTE、STOP、状态输出 |
| `App/control/control_policy.c/.h` | 仅新增正常Disarm/REMOTE授权/源枚举 |
| `App/control/p3b_config.h`（必要时新增） | 精简的P3b阶段门禁 |
| `Projects/RTnode.uvprojx` | 仅为新增.c调整，保留P3a编译项 |
| `App/control/local_follow.[ch]` | 默认保持原样 |
| `App/actuator/*`, `App/imu/*`, `Driver/imu/*`, `Core/Src/interrupts.c`, HAL, FreeRTOS port | 只读，无真实缺陷不碰 |
| `doc/P3b/*` | 实施后回填交接和必要接口实况 |

禁止多个并发Codex对话同时修改 `control.c`、`can_app.c`、Keil工程；用户负责上板实测。
