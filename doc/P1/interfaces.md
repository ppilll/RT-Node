# P1a Interfaces v1.0

## 1. 范围

只定义低层执行器 API；**P1a 不建立远程/本地控制源协议**。P1b 将 `ControlTask` 设置为正常路径的唯一所有者。旧工程 `actuator.h` API 名字优先保持兼容；必要时局部补充状态字段、返回码，但须在交付报告中记载 ABI/API 变更。

## 2. 数据结构/单位

旧设计：

- `ActuatorCommand { int32_t pan_position_us; int32_t tilt_position_us; }`：两轴**绝对目标高脉冲宽度，单位 µs**，非角度、非 PWM 占空比百分数、非图像误差。
- `ActuatorApplyResult`：requested 两轴原始值、applied 两轴最终限幅 µs、各轴是否 saturated。建议保持旧字段。
- `ActuatorSnapshot`：initialized/enabled、最近 applied µs、command/enable/disable/saturation/fault 计数；**建议最小增补** `fault_latched` 状态或等效明确状态字段，避免硬件已关但快照仍称输出使能。明确 32 位计数器自然回绕。
- 安全范围和电气边界：旧注释的电气 envelope 500–2500 µs；本阶段使用 Pan 1000–2000 µs、Tilt 1200–1600 µs、中位 1500 µs。**若参数输入超过软件安全范围，限幅并报告，不在数值未审核前直接透传。**

## 3. 公开 API 约定

| API | 输入/返回 | 允许上下文 | 主要契约 |
|---|---|---|---|
| `bool Actuator_InitSafe(void)` | bool | 启动期、非 ISR | 安全初始化、默认关断；重复初始化不得绕过锁存或使能状态；错误返回 false 并关断 |
| `bool Actuator_Enable(void)` | bool | 受控测试主循环（将来 ControlTask） | 需初始化、未故障锁存且有测试授权；双轴预装 1500 µs，不允许旧命令自动恢复；失败关断并可锁存 |
| `bool Actuator_Apply(const ActuatorCommand *cmd, ActuatorApplyResult *result)` | cmd 必须非 NULL；result 可 NULL | 非 ISR，唯一正常调用者 | 仅 enabled；执行独立限幅并同步两路预装比较值；返回 true 表示寄存器请求成功，不等于机械运动完成 |
| `void Actuator_Disable(void)` | void | 非 ISR/正常测试入口 | 幂等、强制关断/引脚低；不擦除 fault latch；旧命令失效 |
| `void Actuator_ForceSafeOutput(void)` | void | 启动早期、异常/紧急调用 | 无 HAL/RTOS 等待，先快速关 CCER/CEN，再 GPIOC 低；紧急路径锁存故障，尽力而为；不可要求返回或阻塞 |
| `void Actuator_GetSnapshot(ActuatorSnapshot *snapshot)` | 可 NULL（无操作） | 正常线程/测试观测 | 返回内部状态的一致性说明；需要短临界区时应恢复调用者原本中断屏蔽状态，而不是无条件开中断 |

接口文档中的“有测试授权”须由调用者落实，不建议在底层驱动隐式读取串口/按键/远程协议；P1a 最小测试入口必须默认关闭并能在编译配置或明确步骤下开启。避免让 P1a 测试入口成为 P1b 永久绕过点。

## 4. 状态转移（建议）

| 当前状态 | 事件 | 下一状态/输出 |
|---|---|---|
| Reset / UNINITIALIZED | `ForceSafeOutput` | 关断，若是紧急故障路径则锁存；避免普通复位初始化被错误锁存 |
| UNINITIALIZED | `InitSafe` 成功 | DISABLED，PC6/PC7 低，无 PWM |
| UNINITIALIZED / DISABLED | `InitSafe` 失败 | FAULT_LATCHED，关断 |
| DISABLED | `Enable` + 明确授权 | ENABLED，先中位 |
| ENABLED | `Apply` | ENABLED，限幅，双通道按更新事件生效 |
| ENABLED / DISABLED | `Disable` | DISABLED，硬件关断；如果已故障则保留故障态 |
| 任意 | 紧急故障 | FAULT_LATCHED，硬件关断 |
| FAULT_LATCHED | `Enable` / `Apply` / 重复 Init | 拒绝使能，保持关断 |
| FAULT_LATCHED | 整机复位 | UNINITIALIZED；重新履行安全启动程序 |

> 关键实现细节：早期安全关断与**故障锁存**不是同一个概念。不要让 `main()` 启动时调用硬件安全关断就误把正常冷启动永久标记 FAULT。推荐内部硬件 `ForceLow` 原语 + 显式 fault wrapper，公开 API 语义要一致且可测试。

## 5. 数据所有权与并发

- P1a 仅单一测试执行路径正常调用 `Enable/Apply/Disable`；不存在正式 RTOS 任务。
- 中断/异常只允许触发紧急关断，不得向 TIM3 写正常运动目标。
- 在紧急关断与正常 `Apply` 可能交错时，先锁存故障，正常路径在写入前/使能前检验锁存；安全竞态处理须有明确最坏窗口，不能仅凭 `volatile` 断言完全无竞争；必要时采用短临界区并保证异常关断优先。
- `Actuator_GetSnapshot()` 诊断计数不得延迟硬件关闭；异常处理里可以只维持最少的状态锁存，完整日志留待安全上下文处理。

## 6. 兼容性与禁止入口

保持原 API 名称、命令单位、初始限幅宏和双通道行为；不要迁入旧 `gimbal_control.h` 与 `protocol_transport.h`，不要新增绕过安全授权的全局 `TIM3` 写接口。未来 P1b 的 `ControlTask` 复用公开 API，但不在此阶段实现调度。
