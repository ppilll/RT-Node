# RT-Node F103 — P2 设计基线 V1.0

- 评审日期：2026-10-17
- 源码基线：`ppilll/RT-Node`，`main` HEAD `c9d48211a11ed60f949e67da49cefff3a9d1a897`（P1b）；Codex 开工必须复核，不得假定未变。
- 本文件性质：**用户已确认的总体硬件与精简开发原则 + P2a 编码依据**。未完成的实机测试不得记为 PASS。
- 范围：P2a = SPI IMU 采集/姿态估计；P2b = 后续显式授权的本地双轴跟随。先 P2a 验收，再 P2b 设计批准/编码。

## 1. 确定的输入与系统架构

- STM32F103ZET6，正点原子精英 V2；Keil MDK5 / ARM Compiler 5.06u7 / HAL / FreeRTOS，72 MHz，1 kHz RTOS tick。
- 传感器为成品 ICM20602 8 引脚模块，**独立手持姿态输入设备**，经较短导线直接连接本板。用户确认 **VCC 使用 3.3 V**。
- 模块照片正面丝印从上到下：`VCC`, `GND`, `SCL/SPC`, `SDA/SDI`, `SA0/SDO`, `CS`, `INT`, `FSY`。末脚丝印是 `FSY`，不是此前文字中的 `FST`；结合芯片引脚命名视为 FSYNC 的板级缩写，使用前由操作者核实该模块未作特殊复用。
- 使用 SPI1 默认复用、软件片选：PA5 SCK，PA6 MISO，PA7 MOSI，PC4 CS。模块 VCC→3.3 V，GND→GND；INT 不接；确认 FSY=FSYNC 时将 FSY→GND（芯片规格建议未使用时接地）。**勿把 SPI MOSI/MISO 按 I²C 的 SDA/SA0 用法接反**。
- 上电、停机和致命故障仍按 P1 的执行器规则；保留 TIM3 全重映射 PC6/PC7、50 Hz、20 ms ControlTask、Actuator 正常独占写入与故障锁存。
- SPI1 未来与其它外设资源复用冲突需在后续阶段另评，本阶段不启用摄像头相关 PC4 功能。

## 2. P1 不变量和真实源码边界

- `App/actuator/include/actuator.h`, `App/actuator/src/actuator.c`：唯一 TIM3/PC6/PC7 底层所有者；Pan 1000..2000 us、Tilt 1200..1600 us、中位 1500 us。错误 `Actuator_ForceSafeOutput()` 锁存，复位前不清除。
- `App/control/control.c`：`ControlTask` 静态创建，现优先级 `tskIDLE_PRIORITY+1`，20 tick 周期、独占正常 Enable/Apply/Disable；DWT/调度保护已存在。P2a 不更改该文件。
- `App/control/control_policy.h/.c`：实际只有 `CONTROL_SOURCE_NONE/TEST`，`ControlSession` 为单次授权、terminal 不可重新授权；P2a 不增加 LOCAL 源。
- `Core/Src/main.c`：上电前置安全输出、时钟/RTOS tick handoff 均冻结；P2a 只允许在现有初始化流程中最小加入 ImuTask 静态创建入口，且不使能舵机。
- `Core/Inc/FreeRTOSConfig.h`：静态分配启用、动态分配禁用、`INCLUDE_xTaskDelayUntil=1`、`INCLUDE_vTaskDelay=0`；P2a 优先避免修改此文件。
- `Core/Inc/stm32f1xx_hal_conf.h` 已定义 `HAL_SPI_MODULE_ENABLED`；`Projects/RTnode.uvprojx` 尚未编入 `Driver/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c`，P2a 需要在原工程增加这一现有 HAL 源文件。
- 不允许替换 P1 main 时钟/启动文件、FreeRTOS port、异常路由或整体 HAL 配置。

## 3. P2a 冻结的最小实现方案

| 项 | 决定 |
|---|---|
| 接口 | SPI1，4-wire Master，Mode 3 (CPOL=1/CPHA=2EDGE)，8-bit，MSB first，NSS 软件管理 |
| 频率 | APB2=72 MHz /128 = **562500 Hz** 候选编译期固定值 |
| 采集 | 单 ImuTask，目标每 10 tick 一次（100 Hz），周期轮询，无 DMA/FIFO/DMP/INT |
| 量程 | 加速度 ±2g，陀螺仪 ±250 deg/s；解码分别用 16384 LSB/g、131 LSB/(deg/s) |
| 带宽 | `CONFIG=0x03`（陀螺仪约 41 Hz），`ACCEL_CONFIG2=0x03`（加速度计约 44.8 Hz） |
| 采样率 | 1 kHz 内部基础分频，`SMPLRT_DIV=9`，目标输出 100 Hz；必须按官方 FCHOICE/DLPF 组合核实 |
| 事务 | `0x3B..0x48` 一次 SPI burst，读取 14 字节数据（另发 1 字节地址），大端二补码；`WHO_AM_I(0x75)=0x12` |
| 运动学 | 以模块/芯片标定轴为基准，只估算 Roll、Pitch；陀螺零偏与互补滤波；使用测得 dt |
| 发布 | 单写者 ImuTask + 完整静态姿态快照，短临界区复制，禁止读到半更新结构 |
| 调度 | **P2a ControlTask 保持原优先级 1；ImuTask=Idle 优先级 0**。接受采样抖动按真实 dt 处理，不提前改 P1。若测试发现不可接受，再提变更审查。 |
| 错误 | SPI 有界超时，初始化识别有限重试；失败标记 FAILED/invalid，首版不做自动复杂恢复；P2a 执行器始终关闭 |

SPI 连续读线速理论最低 `15*8/562500 ≈ 0.213 ms`，这不是实际 HAL 调用、RTOS 唤醒或算法 WCET。拟用单事务 3 ms 超时上限（**候选，由 Codex 查实项目 HAL 实现并落实总线恢复/CS 释放**），不得沿用厂商 500 ms，也不得在串行多阶段用无界等待。SPI 失败时不允许“沿用有效标记”；最后角度可供诊断显示，但 `valid=false`。

## 4. P2b 边界（本次不开发）

- 采用手持姿态相对参考位置 → 绝对 `pan_us/tilt_us` → 死区/速率限制/软限幅 → ControlTask → Actuator，Roll/Pitch 对应 Pan/Tilt 为初始候选，最终轴向/符号、增益、机械限位须在 P2b 无负载测试前确认。
- 不承诺仅凭六轴 IMU 获取长期稳定的绝对 Yaw；若要求“转动身体方向→云台航向”，需另评传感器/算法。
- `CONTROL_SOURCE_LOCAL_IMU` 与 `LOCAL_FOLLOW` 运行模式都仅在 P2b 按总控批准新增；不能把本地源冒充 TEST，不能清除 P1 故障锁存。
- 启动默认无 PWM；必须有显式授权；姿态过期或设备失效关闭输出；恢复通信不能自动重放旧命令；P1 受影响接口必须回归。
- P2a 不改 `control_policy.*` 或 `Actuator.*`，不写 P2b 动作映射。

## 5. 只保留必要防御与测试

- 必须：有界 SPI 传输、初始化失败有界结束、WHO_AM_I 和关键配置校验、数据样本/时间戳/有效标记、短临界区快照、P2a 禁止 PWM、P2b 原有限幅和故障关闭。
- 不做：复杂 SPI/IMU 自动修复框架、UART DMA、GPIO trace 大框架、DMA/FIFO/DMP/EKF、海量故障注入矩阵、全部内部性能指标外部读取系统。
- P2a 最小 Gate：正确构建、纯 C 算法主机测试、实机 SPI ID/14 字节采集、设备缺失有限失败、上电 PC6/PC7 无 PWM、记录证据；不具备数据的测量项填 NOT_RUN。详见 `doc/P2a/test_plan.md`。

## 6. 证据与来源

- TDK 官方 IC M-20602 Datasheet DS-000176 Rev 1.1：<https://invensense.tdk.com/wp-content/uploads/2020/11/DS-000176-ICM-20602-v1.1.pdf>
- ST RM0008 SPI/GPIO/AFIO：<https://www.st.com/resource/en/reference_manual/cd00171190-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- 工程：<https://github.com/ppilll/RT-Node>；该文件中的 HEAD 只是一份审查快照。
- 用户提供逐飞厂商原始 RAR SHA-256：`39d641567aae438324e12bf9440aa2a7b1a461979cd922ef6f20e091214ba791`；是参考资料不是可直接移植的 P2 驱动。

## 7. 开工条件与版本管理

- 已确认：型号、3.3V、8 引脚照片、手持短线、SPI 通信、厂家源码包、精简原则。
- 开工时操作者确认：按丝印从 `VCC` 到 `FSY` 接线；电源关闭状态完成接线；没有额外外设驱动 PC4/PA5..PA7；在 SPI1 读取通过前与舵机断开。
- 模块 FSY 以 FSYNC 处理是根据丝印和通用芯片引脚的高可信推断；如供应商板级原理图说明不符，暂停该引脚接线并修订。
- 不在本阶段自动进入 P2b/P3；P2a 完成后必须输出交接记录。
