# Codex 对话任务 — P2a（单一会话，按步骤串行）

## 一、任务说明

**任务 ID：P2a-C1 — ICM20602 SPI 驱动、Roll/Pitch 融合、ImuTask + Keil 集成。**

- 执行方式：单个 Codex 会话，负责全部 P2a 文件与共享工程集成，避免多会话冲突。
- 前置条件：使用当前 RT-Node 仓库；检查 HEAD 与未提交变更；读取本包及 P1 源码。IMU 厂商包在 Resources 是用户后续放入的参考资源；不存在不阻断核心驱动开发，但必须如实报告。
- 允许新建：`App/imu/**`、`Driver/imu/**`、P2a 主机测试和证据文档。
- 允许最小修改：`Core/Src/main.c`（增加 Imu_Init）、`Projects/RTnode.uvprojx`（新源、HAL SPI 源、include path）。
- 禁止：修改 P1 `App/actuator/**`、`App/control/**`、`Core/Src/interrupts.c`、`Core/Src/rtos_hooks.c`、FreeRTOS port/时钟安全逻辑；实施 P2b、本地跟随使能、任何舵机自动运动；重置用户仓库状态。
- 构建/主机验证真实结果和缺失测试必须回填 handoff。

## 二、直接复制给 Codex 的启动提示词

```text
你负责 RT-Node F103 的 P2a 实施，先读 doc/P2/P2_DESIGN_BASELINE_V1.0.md 和 doc/P2a/README.md 中列出的全部文档，然后读现有 main/ControlTask/Actuator/FreeRTOS/Keil 工程真实源码；核对当前 main HEAD（设计快照为 c9d48211a11ed60f949e67da49cefff3a9d1a897），保护现有未提交改动。遵守仓库实际 AGENTS.md（若有）。

目标：实现 SPI1 连接 ICM20602 的 100Hz 原始采样、静止陀螺零偏、纯 C Roll/Pitch 互补滤波、静态 ImuTask 和完整姿态快照，并集成已有 Keil ARMCC5 工程。硬件：STM32F103ZET6，3.3V ICM20602 模块，短线；PA5 SCK、PA6 MISO、PA7 MOSI、PC4 CS（Mode3、/128），INT不用，FSY作为FSYNC待用户最终接线确认。P2a 默认且始终不使能舵机。

必须保留现有 P1 20ms ControlTask 优先级1、TIM3 PC6/PC7 输出逻辑、Actuator 故障锁存、SysTick 与异常处理。ImuTask 初期优先级0，静态分配；不改变 FreeRTOS 内核配置。新增实际 HAL SPI C 源进入原 uvprojx，工程 HAL_SPI_MODULE_ENABLED 已经启用。厂商 RAR 若在 Resources/vendor/ICM20602 可核对，若不存在报告但不要声称已阅读仓库资源；切勿复制厂商 HAL/main/启动代码。

ICM20602：WHO_AM_I(0x75)=0x12；复位/等待/识别有界；复位后配置 I2C_IF(0x70) Bit6，SPI only；量程±2g/±250dps，比例16384 LSB/g和131 LSB/dps；CONFIG=0x03、ACCEL_CONFIG2=0x03、SMPLRT_DIV=9；使用一次从0x3B读取14字节的burst。厂商源存在GYRO_CONFIG=0x18但换算仍除131的错误，不准照搬；HAL SPI 500ms超时/无限循环亦不准照搬。任何 SPI 错误释放CS并标记无效，不要引入复杂自动恢复/DMA/FIFO/DMP/EKF。

按 implementation_plan.md 顺序：静态检查、独立新模块、最小集成、主机测试、ARMCC构建、测试指南与 P2a_HANDOFF；功能和结果须满足 requirements.md/interfaces.md/test_plan.md。主机测试必须运行真实姿态融合代码。没有硬件现场时全部 LA/机械测试记 NOT_RUN，绝不填虚构结果。不批准 P2b。若发现与 P1 安全/控制接口冲突，先停止并报告选项，不自行重写 P1。
```

## 三、Codex 本轮结束时必须答复

1. 当前 HEAD、工作树状态及与设计快照是否存在差异。
2. 新增和修改文件清单（标识共享文件），P1 保护文件 diff 应为空。
3. 实际寄存器配置、SPI 模式/频率、IMU 任务优先级/栈、校准与快照语义。
4. ARMCC Rebuild 日志/ROM/RAM/HEX Hash；主机单元测试 PASS/FAIL 与证据。
5. 未执行硬件测试列表 `NOT_RUN` 和用户操作步骤。
6. `P2a_HANDOFF.md` 与是否建议 `P2a ACCEPTED`（无实机证据不得接受）。

在收到本任务后，Codex 不需要再向用户反复索取芯片手册；官方 DS-000176 v1.1 已列于文档。若 FSY 的实物电气连接与推断不一致，暂停该引脚接线，不阻塞 SPI 基本测试。
