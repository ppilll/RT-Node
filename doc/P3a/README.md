# RT-Node F103 — P3a Codex 入口（V1.0，接口冻结）

日期：2026-10-17。目标仓库：`https://github.com/ppilll/RT-Node`。设计基线：默认分支提交 `59e4a0b0ebf738a8221f719703948e798c6d7d47`（P2b）。**执行前仍须核对 HEAD，若有新增提交，先比较相关文件再动手；不要在旧版本上覆盖新修改。**

## 本轮已获总控批准的决定

1. 正常 STOP/超时后，允许重新授权；驱动/调度/时钟等致命故障仍锁存，只能复位。
2. REMOTE 可由 MODE_REQUEST + 新 epoch 内的新鲜控制命令授权；LOCAL 仍需 KEY0 明确按下。
3. REMOTE_STOP 对 LOCAL/REMOTE 全局有效。
4. 运行中允许切换，但必须先 Disable、使旧命令失效，再按新 epoch 和新授权启动。
5. CAN 协议为 5 个标准 ID、DLC=8，控制 TTL=100ms、心跳周期100ms/超时500ms、STATUS 周期100ms。
6. 远程变化上限先用每20ms ±2µs，继续复用 Actuator 最终脉宽限幅。

以上为 **P3 全阶段接口与控制语义冻结**。本包只交付 **P3a** 实现任务：CAN 收发/协议/最小 STATUS；不实现上述 P3b 动作与执行器控制。

## Codex 阅读顺序

1. 仓库中的适用 `AGENTS.md`（本次 GitHub 读取时，仓库根目录和所检查子目录未找到；执行时应重新检查）。
2. 本目录 `README.md` → `requirements.md` → `interfaces.md` → `design.md` → `implementation_plan.md` → `test_plan.md` → `codex_tasks.md`。
3. `doc/P2b/design.md` 和 `doc/P2b/interfaces.md`。
4. 现有 `App/control/*`、`App/actuator/*`、`App/imu/*`、`Core/Src/main.c`、`Core/Src/interrupts.c`、`Core/Inc/FreeRTOSConfig.h`、`Projects/RTnode.uvprojx`。

## 无法擅自填补的硬件参数（实施门禁）

**2026-10-17 更新：用户已明确批准用于实测的配置为 500000bit/s、APB1=36MHz、Prescaler4、BS1=14TQ、BS2=3TQ、SJW1TQ、采样点83.333%。** 参数已写入 `App/can/can_timing.h`，允许生成可刷写的 Stage0 P3a 验证固件；`TIMING_USER_APPROVED=1`、`TIMING_CONFIRMED=0`，用户批准不代表实机已通过。RK3588 为 ATK-DLRK3588 / can0 / rockchip_canfd / 99MHz，Classical CAN。硬件依据由用户指定为两板官方原理图、板载收发器和端接；实际跳线/接线/端接由用户现场确认。真实 T1 仍 NOT_TESTED，P3a 未 ACCEPTED。最新 HEX、构建证据及步骤见 [P3a_HANDOFF.md](P3a_HANDOFF.md) 和 [T1_PREPARATION.md](T1_PREPARATION.md)。

## 修改授权边界

本包属于 **供 Codex 执行的任务文档**。创建本包没有修改 GitHub 仓库或源码。未来 Codex 执行时，P3a 只允许为通信所必需的最小工程改动；不得启用 PWM，不得修改 `control_policy.c` 实现 P3b，不得修改 Actuator/IMU，不得实施 P4。

## 最终交付

- Git diff / 变更文件清单；Keil 编译结果（实际运行后填写）。
- 一个精简的 T1：RK3588↔STM32 双向帧交换和 STATUS；确认未使能 PWM。
- `P3a_HANDOFF.md`，按 `handoff_template.md` 填写；没有现场证据的项目写 `NOT_TESTED`。

官方参考：
- https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
- https://docs.kernel.org/networking/can.html
- https://github.com/linux-can/can-utils
