# P2a 最小验证计划（无 SWD；不做大规模防御性测试）

## 1. 工具与证据分类

环境：Keil MDK5 ARMCC5.06u7 构建；Windows/Linux 主机 C 编译运行；CH340 串口 Bootloader 下载；逻辑分析仪测 SPI1/PC4/PC6/PC7；人工静止与正负倾斜。**没有 SWD/J-Link、示波器、已批准的应用 UART 日志或任务 trace GPIO。**

测试结果严格使用 PASS / FAIL / NOT_RUN，并明确：`BUILD`、`HOST`、`LA`、`MANUAL` 四类证据；主机测试不能代替实机执行证据。逻辑分析仪不能代替电压/模拟信号测量。验收标准执行前确定，不因运行结果随意修改。

## 2. 必需测试矩阵

| ID | 类别 | 步骤 / 预期结果 | 记录 |
|---|---|---|---|
| B01 | BUILD | 用真实 `Projects/RTnode.uvprojx` Rebuild，0 error；记录新警告/ROM/RAM/HEX Hash | build.log、map、sha |
| S01 | STATIC | P1 `App/actuator/*`, `App/control/*`, interrupts 与 P1 安全时钟逻辑未改；静态任务不调用 Enable/Apply | diff/check |
| U01 | HOST | 连续14字节含负数二补码的已知向量解码完全一致；`16384` counts=1g，`131` counts=1dps | host output |
| U02 | HOST | `imu_fusion.c` 真实算法：水平静止及 ±15° Roll/Pitch 合成数据，在稳定收敛后误差≤2° | fixtures/results |
| U03 | HOST | 角速度零偏与 dt=5/10/15ms 序列，长时间水平后不过度漂移、返回水平；按预定 fixture 误差≤3° | fixtures/results |
| U04 | HOST | 无效原始数据、0或>50ms dt、初始化失败/模拟 SPI 超时进入 invalid/FAILED、不会写半帧；全路径有界 | host output |
| H01 | LA | 烧录 P2a，捕获 CS/SCK/MOSI/MISO：Mode3、约562.5kHz，MOSI `0xF5`，MISO ID `0x12`；配置写入可见 | 原始 LA、解码截屏 |
| H02 | LA+MANUAL | 捕获 MOSI `0xBB` + 14byte 返回；约10ms 间隔候选；水平/倾斜/回正原始加速度符号与大小合理 | LA CSV/原文件 |
| H03 | LA | PC6/PC7 上电及 ImuTask 运行期间无 50Hz PWM，静态低电平；舵机电源/信号断开 | LA 截图 |
| H04 | LA+MANUAL | 断电接线状态下移除 IMU 后再上电，有限探测后停止正常采集、不会引起舵机输出；不实施带电拔插 | LA + 操作记录 |

默认所有 Test ID 状态 `NOT_RUN`；Codex 只能按真正执行/证据填写。H02 的 SPI 访问间隔是采集活动代理，**不能替代任务精确唤醒时间**。不要求独立测 ImuTask 最坏执行时间，需如实记未测。

## 3. 主机测试实现约束

- 主机直接编译运行真实 `imu_fusion.c`、原始数据解码函数（可抽离到纯 C），HAL/SPI 由 mock 模拟返回/失败。
- U02 使用加速度由标准姿态重力投影生成（水平/±15° Roll/Pitch），零陀螺角速度；以连续 100Hz 更新足够长至收敛（例如3s），计算 Roll/Pitch 绝对误差。
- U03 角速度常偏置+已知积分/返回；dt序列无固定10ms假设；明确 fixture、期望和容差，不以截图代替数值。
- U04 可以在单测里模拟 `WHO_AM_I` 错、SPI 超时、半帧，不需在真机把总线强行拉到电源/GND。

## 4. H01/H02 信号采集具体要求

- 逻辑分析仪 GND 接开发板 GND；CH0 PC4、CH1 PA5、CH2 PA7、CH3 PA6；SPI Mode3，MSB-first，采样率 ≥5MS/s 候选。
- 保存工具原始 capture 文件或可重复导出的 CSV（含所有通道和时间戳）、相关屏幕截图，记录分析仪型号/采样率/阈值设置。
- H01 若 ID 错，先检查 SPI MOSI/MISO 接反、CPOL/CPHA、上电 CS、3.3V 与 GND；不可直接把 ID 通信活动判作 READY。
- H02 至少保存 10 个连续 burst 与手动转动前/后各一段原始字节，离线解码得到 g/dps；没有角度标定装置，不宣称绝对姿态高精度。
- H03 将探头移至 PC6/PC7，记录无 PWM；如果采样窗口太短不可宣称“永远无 PWM”，至少覆盖启动与持续采集阶段约数秒。
- H04 必须 MCU 断电才拆接模块，避免热插拔冲击。断开传感器的“失败有界”以 SPI 停止/可见状态+代码证据判断，若外部无法确定终止时间只声明有限逻辑已测试，不给出虚构毫秒值。

## 5. 记录模板（每项一行）

```text
Test ID:
Git commit / HEX SHA-256:
编译宏 / 固件目标:
日期 / 操作者:
测试类别: BUILD | HOST | LA | MANUAL
设备及接线 / 逻辑分析仪采样率:
输入操作:
预期:
实际:
证据路径:
结果: PASS | FAIL | NOT_RUN
限制/说明:
```

## 6. P2a Gate

- 工程构建+主机算法 B01/S01/U01-U04 PASS；实机 H01-H04 全部必要项 PASS；原始证据保留；P2a 默认执行器禁用，P1 无不可接受回归。
- 若操作者尚未提供真实硬件记录，交接结论为 `P2a IMPLEMENTED / HARDWARE NOT_RUN` 或 `P2a PARTIAL`，不能写 `ACCEPTED`。
- 对 RAM 中的栈水位、DWT 最坏执行周期若无读出路径，写 `NOT_MEASURED`，不根据静态 MAP 推断实机余量。
