# P2a 需求与验收范围

## 功能需求

| ID | 要求 | 验证 |
|---|---|---|
| A01 | SPI1 默认 PA5/PA6/PA7，PC4 软件 CS，Mode3，初期 562.5kHz。CS 不活动时保持高。 | 源码 + SPI LA |
| A02 | 有界初始化：上电等待、`WHO_AM_I=0x12`、软件复位、SPI-only、配置及有限回读，失败退出。 | 源码 + LA |
| A03 | 以周期 10 ms 目标读取一次 `0x3B` 起连续 14 字节，解码三轴加速度/角速度。 | 源码 + LA + 主机 |
| A04 | 采用加速度 ±2g、陀螺仪 ±250dps、合理 DLPF，LSB 比例与配置一致。 | 寄存器读回 + 主机 |
| A05 | 静止陀螺零偏校准和 Roll/Pitch 互补滤波，真实 dt，拒绝不合理 dt。 | 主机 C 单测 |
| A06 | 单写者 ImuTask 发布完整快照：角度、时间戳、generation、valid 和状态。 | 主机/代码审查 |
| A07 | ImuTask 静态创建（候选优先级0）；不阻塞 ControlTask；动态分配仍禁用。 | 代码 + 构建 + 有限实机证据 |
| A08 | SPI 失败/设备不在时标记无效；有限失败后不无限重试，无复杂重连状态机。 | 主机 stub + 设备缺失实机 |
| A09 | 默认及 P2a 测试镜像均不调用 `Actuator_Enable/Apply`，PC6/PC7 PWM 仍关闭。 | 源码 + LA |
| A10 | 主机算法、构建资源、原始 LA 证据及测试状态可交接。 | 文件 |

## 不做的工作

- 不增 `CONTROL_SOURCE_LOCAL_IMU`、不改 `ControlSession`、不改 P1 正常执行器所有权、故障锁存、TIM3 寄存器策略。
- 不创建 UART DMA、CAN、ADC DMA、OLED、I²C2、HAL SPI DMA/IRQ、IMU FIFO/DMP、动态消息分发框架、EKF。
- 不做无线手持；传感器短线连到同一主板。
- 不在没有明确授权的情况下为了测时序而占用新的 GPIO 诊断引脚。
- 不把主机合成数据测试误记成真实芯片精度验收。

## 接受标准

- 目标源码能在当前 Keil ARMCC5.06u7 工程编译链接（0 errors；警告需解释并修复引入警告）。
- 正确解码 14 字节且单位换算与量程一致；纯 C 算法按 `test_plan.md` 最小矩阵测试 PASS。
- **只有操作者提交实机证据**才能把 SPI/无 PWM 的实机测试由 NOT_RUN 升级为 PASS。
- 完成 P2a 所有必要实机测试前不得认定 `P2a ACCEPTED`，不得开始 P2b 带载测试。
