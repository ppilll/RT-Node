# P5b 模块只读数据接口（建议）

## 优先直接复用

| 内容 | 既有公开接口 | 注意 |
|---|---|---|
| 环境 T/H/P | `EnvApp_GetSnapshot(EnvSnapshot*)`（P5a） | `valid`、`sample_tick`、定点单位 |
| IMU 姿态 | `Imu_GetSnapshot(ImuAttitudeSnapshot*)` | `roll_deg`、`pitch_deg`、`sample_tick`、`valid`、`state` |
| 控制权/远程状态 | `Control_GetStatus(ControlStatusSnapshot*)` | `state`, `enabled`, `epoch`, `remote_fresh`, `remote_tick`, `reason` |
| 实际下发的 PWM | `Actuator_GetSnapshot(ActuatorSnapshot*)` | `applied_pan_us` / `applied_tilt_us` 是软件输出目标；`enabled=false` 不应显示运行中 |
| ADC | `AdcApp_GetLatest(AdcApp_Result*)` | 可选展示 avg/mv 等，不改变 ADC DMA |

不要为了 UI 重建复杂消息总线；所有 getter 以任务上下文完整复制，不返回静态内部指针。已有 Control/IMU/ADC getter 的原有同步模式优先复用。

## 最小新增摘要（需要显示真实接收元数据时）

```c
/* 以下为建议字段：实现时可按 128x32 页面需要裁剪，不改既有 CAN 线上协议。 */
typedef struct {
    uint32_t last_id;       /* 最近一条被 CanTask 处理的帧（明确定义“最近”） */
    uint16_t requested_pan_us, requested_tilt_us; /* 仅控制帧时更新 */
    uint32_t last_rx_tick;
    uint32_t rx_count;
    bool     valid;
} CanDisplaySnapshot;
bool CanApp_GetDisplaySnapshot(CanDisplaySnapshot *out);

typedef struct {
    char     last_command[16]; /* 截断并以 \0 结束的已解析命令，不读取 DMA 原始环形区 */
    uint32_t rx_bytes;
    uint32_t ovf_count;
    uint32_t last_command_tick;
    bool     valid;
} UartDisplaySnapshot;
bool UartApp_GetDisplaySnapshot(UartDisplaySnapshot *out);
```

CAN 快照由 CanTask 在**实际 ProcessFrame** 解析处更新；要说明记录的是“原始收到 / 格式通过 / 控制接受”的哪一种。若选择“最近合法控制帧”，不能误导成 ControlTask 已接受或已执行。UART 快照由现有 UartTask 发布，最近文本经过裁剪和 NUL 终止；`COUNT?` 统计语义不可更改。优先只新增 UI 确实要显示的字段。

## 控制停止状态

可将一个只读 `bool key0_stop_latched` 加入 `ControlStatusSnapshot` **或**通过小 getter 获取，但须在 ControlTask 内单一来源发布，短临界区安全复制；不可把 `reason==0x01` 当成可靠的永久锁存证明，因为其他协议事件可能改写原因值。

## 显示模块内部

`DisplayApp_Init()` 创建静态 DisplayTask；内部有当前页 `0..3`、512B framebuffer、单个 I²C1 HAL Handle、KEY1 消抖状态。初始化放在合理任务上下文；对外无需通用绘图 API 或页面更改控制接口。异常显示可选择保持旧内容或清屏，但不能阻塞控制路径。

## 2026-10-18 实施接口

- CanDisplaySnapshot实际包含last_id/last_rx_tick/rx_count/request_tick/requested_pan_us/requested_tilt_us/valid/request_valid。Getter沿用建议名称，false时也完整复制零初始化/历史结构。最近帧指所有CanTask出队进入ProcessFrame的帧，含被拒格式/内容；请求仅在STD/DATA/DLC8的0x180更新，不承诺范围/epoch/seq通过或Control接受。请求与帧元数据同一短临界区发布。
- UartDisplaySnapshot使用建议字段。记录换行结束的非空可打印ASCII行，包括未知命令，前15字符截断并NUL终止；丢弃/超长行不伪造摘要。rx_bytes/ovf_count沿用COUNT?语义，由UartTask发布，Getter不读DMA。
- ControlStatusSnapshot增加bool key0_stop_latched，由ControlTask唯一发布，复位前不清。CAN STATUS仍手工打包原有8字节；reason=0x01数值沿用物理按键停止含义，来源改为KEY0。
- AdcApp_GetLatest最终发布和复制补短临界区，支持DisplayTask第二个读取者；只有UartTask调用Process。
- Core/Src/i2c_msp.c提供唯一强定义HAL_I2C_MspInit，按I²C1/I²C2分派PB6/7或PB10/11。
