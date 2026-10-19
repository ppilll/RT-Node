# P07_RESUME_EVIDENCE · 简历证据映射（模板）

> 本文件仅在拿到真实测试后填写，**严禁把阈值/配置当实测结果**。

| 简历技术点 | 代码事实（路径/函数） | 证明方式 | 实测数据及来源 | 可对外宣称 | 局限 |
|---|---|---|---|---|---|
| ICM20602姿态融合/双轴手势目标 | `ImuTask` / `ImuFusion` / `LocalFollow_Target` | G-A/G-B，可选G-C | 待填 | 姿态融合、限幅限速的实现；通过了哪些证据 | 无Yaw/机械闭环，Stage0不出PWM |
| 多任务实时性 | `ControlTask`+DWT | T1-A/B | 待填 | 固定负载下观察周期和最大elapsed | 非数学WCET；无对照不写优化% |
| CAN与UART DMA异步通信 | CAN ISR/Queue/CanTask, USART DMA Circular | T2 | 待填 | 指定工况的处理/回复统计 | 发送成功不等于STM32处理；OVF非丢字节数 |
| IWDG/持久化安全授权 | HealthTask/Flash state | P6历史实机证据 | 待填 | 有回执的复位/安全锁事实 | 监督仅看wake_count；LSI偏差 |
| 静态内存/栈 | MAP/FreeRTOS HWM | M/T1 | 待填 | Flash、RAM、最小剩余栈 | 不等于CPU利用率 |

## 简历A标准版（全部【】只能由真实证据填写）

- 基于ICM20602六轴IMU实现10ms姿态采集、静止标定及互补滤波，将Roll/Pitch相对姿态转换为双轴PWM控制目标，设计±2°死区、±15°限角和2us/周期限速；经【测试级别】验证【真实行为】。
- 基于FreeRTOS实现20ms控制任务与IMU、CAN、UART、ADC/OLED多外设协同调度，使用DWT测得在【负载与时长】下周期偏差【】、最大观测执行段【】和周期违例【】。
- 实现CAN ISR/静态队列/CanTask及USART1 DMA环形接收、任务态协议解析，在【测试输入工况】下测得CAN已处理帧【】、UART PING/PONG【】及异常事件【】。
- 设计HealthTask基于Control进展条件喂IWDG、RCC复位原因识别与Flash持久授权锁定，在【P6真实测试证据】下验证【故障后控制策略】。

## STAR面试B详细版（测试后补）

1. **手势跟随链路**：为什么选互补滤波、近水平参考、死区/限幅/限速；如何证明算法与实际PWM；为何没有Yaw/机械角度闭环。
2. **控制实时性**：ControlTask优先级、DWT入口周期与elapsed、异常terminal后如何保留独立观测、采样数量偏差、通信共存。
3. **异步通信与故障监督**：CAN ISR/queue、UART Circular环、TTL/状态快照，以及IWDG/Flash锁存在故障后阻止自动再授权。

所有定量断言附原始日志路径、单位与测试条件；非同负载的旧版本不做性能提升百分比。
