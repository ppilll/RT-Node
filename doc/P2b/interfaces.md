# P2b 真实源码接口契约（Codex 改动需更新本页）

## P2a 已存在（只读）

- `App/imu/include/imu.h`：`bool Imu_GetSnapshot(ImuAttitudeSnapshot *out)`；完整快照短临界区；非ISR，NULL返回false。
- `App/imu/include/imu_types.h`：`roll_deg/pitch_deg` float 单位度；`sample_tick` uint32 1kHz tick；`generation` uint32；`valid` bool；`state` 枚举 `IMU_READY/IMU_FAILED/...`。
- ImuTask 是唯一写者；P2b 不改变 `Driver/imu` 采集、零偏或融合代码；不启用 UART/DMA。

## P1 实际待扩展

- `App/control/control_policy.h`：现有 `ControlSource` = NONE/TEST；新增常量 `CONTROL_SOURCE_LOCAL_IMU=2`；TEST值保持1。
- `ControlCommandSnapshot` 不增字段：`source/generation/issued_tick/ttl_ticks/valid/pan_us/tilt_us`。脉宽单位微秒、时间单位tick。
- `ControlSession` 已有 `authorization_used/authorized/terminal/has_generation/last_generation/fault`；可追加 `expected_source`，初始化及P1测试保持兼容。单次授权/终止不自动复位。
- `ControlSession_Accept` 仍独立复核命令来源与bound/TTL/generation/range；LOCAL_IMU 必须是明确预授权的来源，不能把任何非TEST来源一概接受。
- `App/control/control.h`：可增加 `CONTROL_WAIT_ARM`、`CONTROL_LOCAL_FOLLOW` 状态；现有 mode 数值/诊断字段尽量兼容，避免破坏历史主机检查。新增统计字段如有必要要明确字节预算。
- `App/control/control.c`：唯一读取 Imu 快照、读 KEY GPIO、下达 Enable/Apply/Disable/ForceSafe，保留原每20ms循环和 DWT保护；未经确认不动 `Control_Init()` 任务优先级及栈大小。
- 新增建议 `App/control/local_follow.h/.c`：可测的纯 C 映射与限速逻辑；不得直接调用 HAL/RTOS/Actuator，不自行打开 PWM。
- 新增 `App/control/p2b_config.h`：编译期测试阶段互斥、增益/死区/方向/速率参数、KEY 引脚候选配置。需要明确 P2b 测试镜像的授权宏防误烧；默认 stage0。
- 视最小需要改 `Projects/RTnode.uvprojx` 添加纯C映射模块；如不用新模块可保持共享文件改动更少，但必须有主机可复用的真实映射函数。

## 建议纯 C API 示例（可小幅调整）

```c
typedef struct {
  float roll0_deg, pitch0_deg;
  int32_t last_pan_us, last_tilt_us;
  bool initialized;
} LocalFollowState;

/* 只能在已验证的 READY/valid 快照授权时调用；初始化中位。 */
void LocalFollow_Start(LocalFollowState *s, float roll0_deg, float pitch0_deg);

/* 不接收无效/过期样本；该条件在 ControlTask 完成。 */
bool LocalFollow_Target(LocalFollowState *s, float roll_deg, float pitch_deg,
                        int32_t *pan_us, int32_t *tilt_us);
```

模型只能作为候选，Codex 需确保 NaN/Inf 不进入 int 量化；所有新接口/单位/调用上下文/失效路径记录在实际 `interfaces.md`。

## P2b实际实现（2026-10-17；覆盖上面建议API）

- `ControlSource`保留NONE=0/TEST=1，增加LOCAL_IMU=2；`ControlMode`保留原0..5，追加WAIT_ARM=6/LOCAL_FOLLOW=7，诊断结构未增加字段。
- `ControlSession`尾部增加`ControlSource expected_source`（任务局部，无新增静态RAM）。原`Authorize()`绑定TEST，新增`ControlSession_AuthorizeLocal()`绑定LOCAL_IMU；共用authorization_used/terminal，不能切换来源或再次授权。Accept保留原TTL1..100、年龄>=TTL拒绝、未来时间戳拒绝、generation半环/消费规则和P1安全范围，只增加绑定来源校验。
- LOCAL命令固定TTL=40、issued_tick=sample_tick；ControlTask每20ticks读取完整IMU快照。READY/valid/有限角/age<40才允许授权；FAILED直接terminal，其他未就绪授权前只等。授权后不合格即ForceSafe+COMMAND fault；重复/旧代际沿用GENERATION fault，不恢复。
- 实际纯C接口：`bool LocalFollow_Start(LocalFollowState *, float roll0_deg, float pitch0_deg)`及`bool LocalFollow_Target(LocalFollowState *, float roll_deg, float pitch_deg, int32_t *pan_us, int32_t *tilt_us)`。有限参考初始化中位1500；非有限输入/差值失败且不修改状态/出参。绝对相对角|delta|<=2°归零（不扣2°），限±15°、乘3、偏移向零截断、方向±1、限1455..1545并每次限±2µs。无Yaw/角回绕展开，近水平授权并避开±180°跳变。
- priming接受新generation但不推进映射last_output。tick>=40且DWT>=2,880,000cycles后才Target/Apply，保留原driver UIF检查；首个目标至多离中位2µs。
- 候选KEY0=PE4/KEY1=PE3，ControlTask内初始化INPUT/PULLUP、低有效，两次连续采样去抖。READY/valid/fresh/有限角后先两次松开再两次按下，长按上电无效。KEY1两次按下正常Disable+terminal，KEY1原始低也阻止arm；同时输入故障优先锁存。terminal不再读键/IMU或输出，无重启接口。
- `p2b_config.h`默认Stage0；Stage1要求`P2B_OUTPUT_STAGE=1,P2B_UNLOADED_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL`。Stage2要求`P2B_OUTPUT_STAGE=2,P2B_G2_EVIDENCE_ACK=1,P2B_OPERATOR_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL`且UNLOADED_ACK=0；本轮无批准、未构建。Stage0拒绝残留许可，Stage1拒绝带载许可，输出阶段与P1A/P1B互斥。宏不替代G1/G2现场证据或物理KEY0。
- Stage0不编译LOCAL GPIO/快照/Enable/Apply；默认工程P1A/P1B=0。历史P1B仅隔离HOST回归，P2a整机禁用P1测试的守卫未改。ControlTask priority1/256words栈、ImuTask priority0、迟到/DWT/2ms预算保持；未加任务、队列、日志、trace或动态分配。

## 不能碰的文件和语义

`App/actuator/include/actuator.h`、`App/actuator/src/actuator.c`；`Core/Src/interrupts.c`、`Core/Src/rtos_hooks.c`、startup、HAL底层、FreeRTOS port；`Driver/imu/*` 与 `App/imu/*` 若无 P2a 真正缺陷证据不修改。异常下原驱动 latch 只复位能清除；正常停机 Disable 不得伪造致命故障。
