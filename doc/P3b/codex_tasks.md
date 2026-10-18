# P3b Codex 执行提示词与会话划分

## 唯一建议：1个Codex主会话

P3b 跨 `App/can/can_app.c`、`App/control/control.c`、`control_policy.c`，并可能调整Keil工程，这些文件存在紧密依赖，不要在多个并行对话中同时修改。单会话完成策略/数据流/编译/简短RK脚本；用户负责实机T2-T4并返回结果。只有与STM32工程完全不重叠的RK端脚本确有需要时，才考虑第二个任务。

## 给Codex直接复制的指令

> 仓库 `ppilll/RT-Node`，现在开始 **P3b ONLY**。用户报告P3a CAN双端T1已通过，实际GitHub读取基线 `b2c1f34595ae125a3b27f2d5143c4202762a705c`。先核对当前HEAD/工作区，阅读适用`AGENTS.md`、`doc/P3b/{README,requirements,interfaces,design,implementation_plan,test_plan}.md`、`doc/P3a/interfaces.md`、P2b设计及当前源码。严格复用已实现的CAN1/ISR/FIFO0/500kbps/静态queue/CanTask，不重构底层。
>
> 本轮实现：`CONTROL_SOURCE_REMOTE_CAN=3`；CanTask写远程完整快照及独立STOP/模式事件，ControlTask唯一裁决MODE_REQUEST并写真实STATUS/epoch；正常STOP/100ms远程TTL/模式切换先Actuator_Disable并清旧命令后可重新授权；致命fault/Actuator latch不清除、不允许重启。GLOBAL_STOP对LOCAL/REMOTE有效且高于正常命令；LOCAL保留KEY0物理新边沿和离线跟随，KEY1两种模式都能STOP。REMOTE必须MODE_REQUEST→新epoch→新的有效20ms控制seq→>=40ms PRIMING→每20ms每轴±2µs→Actuator最终限幅。同generation不得重复交给Policy；普通心跳不续命远程控制。
>
> 更新STATUS线上state0..7及reason/flags，保持原`doc/P3a/interfaces.md`五个ID、DLC8、Little Endian和action/seq半环契约完全不变。特别修复P3a `can_app.c` MODE恒拒、STOP空操作、远程丢弃、STATUS固定禁用状态，移除仅适用于P3a的`P2B_OUTPUT_STAGE!=0`编译阻断，以简单明确的`P3B_OUTPUT_STAGE` 0/1/2区分默认禁PWM、无负载PC6/PC7测量、用户另外批准后的有限带载；不能沿用P2b测试宏堆积长期架构。尽量不动`actuator.c`、IMU、SysTick/fault handlers、CAN IRQ及原HAL/port。
>
> 执行真实Keil5/ARMCC5编译（如环境可用），用最少代码/一份小RK3588 SocketCAN脚本准备T2-T4，先交付Stage1无负载测试操作，**未经用户新的现场批准不进入Stage2受限带载**。输出git diff摘要、实际构建结果、当前T2/T3/T4状态、必要的`P3b_HANDOFF.md`。如果只有编译没有实机测试，明确NOT_TESTED，不写P3b ACCEPTED。不得开始P4，不要大规模重测P1/P2，不增加复杂状态框架或无用审计。

## Codex 自检要点

- 在有STOP_PENDING / fault / expired / overflow的同一个Control周期，不调用Enable/Apply；旧epoch目标和排队请求不越界重放。
- CanTask / ISR从不调用Actuator；所有普通执行器写入只由ControlTask完成。
- ControlSession正常Disarm不清terminal、fault或驱动 latch，P1 TEST单次授权兼容。
- 0x180每次真正接受的新seq才更新received_tick/generation；心跳、旧seq、不合法DLC不能续命。
- STATUS中`epoch,mode,enabled,action_id,bit5`来自同一一致性状态，不因CanTask仅解析成功而误报。
- 本地从DISABLED不依赖网络依然可经KEY0授权；非REMOTE不会把IMU失败错误应用为远程目标，LOCAL不因心跳丢失而停止。
- Keil保留CAN HAL、FreeRTOS queue.c各一份；P3b Stage0默认禁PWM，Stage1用户分离舵机连接，Stage2不能自动执行。
