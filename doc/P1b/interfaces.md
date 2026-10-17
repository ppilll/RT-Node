# P1b 最小接口契约（待 C1/C2 具体头文件定稿）

## 1. 模块边界

`Core/Src/main.c`：启动/时钟/硬件安全初始化，创建静态任务并启用 scheduler。绝不正常调用 Apply。`App/actuator`：唯一 TIM3/PC6/PC7 底层硬件拥有者，沿用六 API 及其行为；不应直接修改被 P1a 测过的寄存器策略。`App/control`（新建最小目录）：ControlTask、最小测试源和快照校验。`Core/Src/interrupts.c`：异常路由；RTOS Tick 集成。`App/p1b_test`（可选）：仅限无舵机的上电诊断，编译宏保护。

## 2. 命令快照

建议（描述性草案，源码依据具体接口冻结）：

```c
typedef enum { CONTROL_SOURCE_NONE = 0, CONTROL_SOURCE_TEST = 1,
               CONTROL_SOURCE_LOCAL = 2, CONTROL_SOURCE_REMOTE = 3 } ControlSource;
typedef struct {
    ControlSource source;
    uint32_t generation;     /* 无符号单写者单调序号，处理回绕 */
    TickType_t issued_tick;  /* 1kHz tick 本地时钟 */
    TickType_t ttl_ticks;    /* >0 且有界 */
    bool valid;
    int32_t pan_us;          /* 绝对目标脉宽，单位 µs */
    int32_t tilt_us;
} ControlCommandSnapshot;
```

此结构只是拟定接口，不授权实现真实 LOCAL/REMOTE 生产者。快照不可跨任务分字段写入；若尚只有 ControlTask 自有 TEST 产生者，可局部持有并保留稳定契约。发布与接收需要明确一致性方法；P1b 不为了未来扩展而引入多写者复杂邮箱。

## 3. 有效性与重放保护

- 非空、source 在当前允许集合（当前只能 TEST）、valid=true、ttl>0、有界 target、命令 generation 必须比最近被拒/失效 generation 更新；`TickType_t` 模数减法比较年龄，注意回绕和 TTL 不能超过半环。
- 从 DISABLED/FAULT/TEST_END 再进入测试需新的显式授权 + 新 generation，老命令不能恢复。
- 测试源不能绕过全局 ForceSafeOutput/fault latch。
- 输出单位微秒；实际是否夹紧由 actuator 底层最终执行并返回结果；不得因正常限幅而声明机械安全。

## 4. 驱动接口约束（已存在的 P1a API）

`Actuator_InitSafe()`：启动时一次初始化/保持禁止。`Actuator_Enable()`：仅明确允许，且首次输出为中位/低帧策略。`Actuator_Apply(cmd,result)`：启用状态且首帧完成后才能更新。`Actuator_Disable()`：正常停输出，丢弃旧目标。`Actuator_ForceSafeOutput()`：紧急锁存。`Actuator_GetSnapshot()`：普通上下文复制，注意 applied 不是已测量输出。若实际本地最新版本语义和上述不同，优先审查真实源码并更新接口文档，不能假设无条件兼容。

## 5. ControlTask 建议内部接口

- `bool Control_Init(void)`（由 main 调用；只初始化静态资源，不启用 actuator）。
- `void ControlTask(void *arg)`，通过 `xTaskCreateStatic` 唯一创建。
- 最小诊断快照：wake_count、late_count、last_period_ticks、worst_execution_cycles/ticks、min_stack_words、fault_reason、last_accepted_generation。此快照内部归一写者，不要求独立上位机协议。
- 不允许应用或测试任务直接访问 TIM3 寄存器（测量引脚除外）；安全关断只有明确授权函数。

## 6. 上下文规则

- 常规 Actuator Init/Enable/Apply/Disable 必须在指定普通上下文，P1b 正常操作均由 ControlTask 单所有者；exception/assert/overflow 必须硬件先禁止输出并进入不返回安全停留。
- `vTaskDelayUntil` 在任务中；不从 ISR 调用。不在关断临界区、ISR、scheduler suspended 状态调用阻塞 API。
- 变量快照若要跨任务，必须使用完整拷贝+最小保护或单元素静态队列，并审核静态内存占用。

## 7. C2 实际接口冻结（2026-10-17）

- 实际头文件为 `App/control/control.h`、`control_policy.h`、`control_config.h`；`Control_Init()` 由 main 在 InitSafe 后调用一次，内部创建唯一静态 ControlTask（priority=1、256 StackType_t words），重复调用返回 false。
- 实际 `ControlCommandSnapshot` 保留 source/generation/issued_tick/ttl_ticks/valid/pan_us/tilt_us；tick 字段固定为 uint32_t，编译约束 TickType_t 必须 32 bit 且 1kHz。只定义 NONE/TEST，不实现 LOCAL/REMOTE。
- TEST 命令由 ControlTask 同一上下文调用 `P1bTest_MakeCommand` 构造完整局部快照，不跨任务发布，无写邮箱或外部生产者。默认固件不编译测试源调用与 Enable/Apply。
- TTL 合法范围 1..100 ticks；正常源 TTL=40。年龄 `now-issued_tick` 按 uint32_t 模数运算，年龄 **>=TTL** 失效，未来时间戳亦拒绝。脉宽必须落入现有 Pan/Tilt 软件范围，再由不变的底层作最终保护。
- generation 采用无符号半环比较：delta>0 且 delta<0x80000000 才更新；允许自然回绕。新 generation 即使校验失败也被消费。重复/旧/半环歧义命令均终止并锁存关闭。
- `ControlSession_Authorize` 为 ControlTask 内部单次会话门禁；外部没有重新授权/模式切换入口。默认不调用它。仅 enable=1、UNLOADED_ACK=1、ARM_KEY=0xC2201500 的编译镜像可消费一次测试授权。测试完成或任一错误后 terminal=true，不能重新授权，不能清驱动锁存，不能 InitSafe 恢复。复位建立新的启动生命周期，不提供跨复位持久化命令/重放协议；测试镜像会在每次复位后重新运行，必须保持无舵机。
- 内部 mode 仅 DISABLED/PRESTART/PRIMING/TEST/COMPLETE/FAULT，是有限测试阶段，不是完整业务状态机。默认 DISABLED；预等待至少2000 ticks；首帧由既有驱动保持，首次 Apply 须同时满足 Enable 后 >=40 ticks 和 >=2,880,000 DWT cycles，仍由驱动检查自然 UEV。
- 任务周期20 ticks；计划唤醒晚于1 tick、相邻唤醒 tick 间隔越出19..21、xTaskDelayUntil 未实际阻塞，均锁存关断，不追赶控制输出。执行观测窗口达到144,000 cycles（72MHz下2ms）关断。DWT 启动/增计检查失败、周期中 DWT 与 tick 相差超过2ms，也关断。以上是软件保护条件，不是实测时间验收。
- `Control_GetDiagnostics(out)` 仅提供任务上下文的完整只读复制，NULL无操作；内部临界区保护。记录 wake/late、最近间隔、窗口最坏 cycles、Control/Idle 最小栈余量 **words**、模式/首故障、接受计数和 generation fence。没有 UART/GPIO/SWD 读出路径，RAM 中存在统计不构成实测证据。
- 正常结束调用 Disable；错误由 ControlTask 调用 ForceSafeOutput 锁存，再关闭会话。fatal/assert/overflow 仍由 C1 原有不返回路径处理。未增加控制来源、队列、软件定时器或额外任务。
