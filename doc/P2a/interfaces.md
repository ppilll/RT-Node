# P2a 接口契约（Codex 需在实现中落到真实头文件）

## 1. 模块职责与调用关系

| 层 | 所有权 | 任务上下文 | 公开给谁 |
|---|---|---|---|
| `Driver/imu/icm20602` | SPI1、PC4 CS、IMU 寄存器 | ImuTask 独占 | ImuTask |
| `imu_fusion` | 校准偏置、滤波数学状态 | ImuTask；主机测试可独立调用 | ImuTask + 主机测试 |
| `App/imu` | 静态任务/快照唯一写者/状态 | ImuTask | main/未来 ControlTask |
| `App/control` | 正常执行器的唯一操作者 | ControlTask | P2a **不更改** |

## 2. 建议结构体和稳定语义

以下为接口契约示意；Codex 可微调命名与私有实现，但必须记录最终头文件，禁止增加多写者通用 bus。

```c
typedef enum {
    IMU_NOT_STARTED = 0,
    IMU_INITIALIZING,
    IMU_CALIBRATING,
    IMU_READY,
    IMU_FAILED
} ImuState;

typedef struct {
    float roll_deg;            /* 芯片坐标系，deg，姿态角 */
    float pitch_deg;           /* 芯片坐标系，deg */
    uint32_t sample_tick;      /* 1kHz RTOS tick，最后一次成功采样完成时 */
    uint32_t generation;       /* 每次快照发布推进，包括 invalid 状态变更 */
    bool valid;                /* 只在 READY 且最新数据/滤波通过时为 true */
    ImuState state;
} ImuAttitudeSnapshot;

bool Imu_Init(void);            /* scheduler 前调用一次；仅静态任务创建 */
bool Imu_GetSnapshot(ImuAttitudeSnapshot *out); /* 任务上下文完整复制 */
```

- `Imu_Init` 创建失败返回 false，供 `main` 沿用 P1 fail-safe 路径；重复创建不能产生第二个任务。
- `Imu_GetSnapshot(NULL)` 返回 false，不做内存访问；没有任何已发布数据返回 false；已有 FAILED/invalid 状态依旧可复制，调用者必须检查 `valid`、`state` 和后续 P2b 的 TTL。
- `sample_tick` 只代表**成功采样**时间；FAILED 发布时不能把旧姿态 timestamp 刷成“新鲜”。`generation` 以 `uint32_t` 自然回绕，消费者通过半环比较或“只取最新”策略处理；不能以重复世代号把旧有效数据恢复。
- Snapshot 只含必要字段；如果引入 `error_code`，采用简要 enum，不设计完整历史故障系统。

## 3. Driver 建议 API

```c
typedef struct {
    int16_t ax, ay, az;
    int16_t temp;
    int16_t gx, gy, gz;
} Icm20602Raw;

typedef enum {
    ICM20602_OK = 0,
    ICM20602_IO_ERROR,
    ICM20602_ID_MISMATCH,
    ICM20602_CONFIG_ERROR
} Icm20602Result;

Icm20602Result Icm20602_Init(void);          /* 仅 ImuTask，有限等待/错误 */
Icm20602Result Icm20602_ReadRaw(Icm20602Raw *out); /* 单次 burst，完整输出 */
```

如果为了避免阻塞采用跨 ImuTask 周期推进初始化，允许把 `Init` 拆成 `StartInit/StepInit` 私有状态函数；对外只要 `Imu_Init/Imu_GetSnapshot` 稳定，不强制同步 Driver_Init 返回。`Icm20602_ReadRaw` 只有全部 14 字节成功时写入出参，不发布部分样本。

## 4. 数学模块接口要求

- `imu_fusion.*` 只依赖 `<math.h>`、stdint/stdbool，**不得包含 HAL、CMSIS、FreeRTOS 头文件**；便于 MSVC/GCC 主机运行同一份真实算法。
- 使用明确的 `dt_s`、加速度 g、角速度 deg/s 输入和 Roll/Pitch deg 输出；有 `Reset/Calibrate/Update` 之类少量函数即可。
- 初始姿态采用加速度倾角；偏置只从静止阶段计算；dt 不合理时由调用者按 `design.md` 策略处置。

## 5. 原有控制接口兼容

P2a 不修改 `App/control/control.h`、`control_policy.h/.c`、`control_config.h`；不添加 LOCAL_IMU 来源，不调用 Actuator 正常 API。

P2b **后续批准时**才能：增加 `CONTROL_SOURCE_LOCAL_IMU`，定义 LOCAL_FOLLOW 模式、角度到脉宽绝对映射、TTL 候选40ms、显式授权、重放与安全关闭。必须保留执行器的唯一正常写者、原有范围及 fatal latch。不允许直接由 ImuTask 写 TIM3、PC6/PC7 或 `Actuator_Apply`。
