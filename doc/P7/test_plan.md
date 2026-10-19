# test_plan · 生产Stage0正式测试矩阵

## 已实际执行的开发验证

2026-10-19：G-A算法/策略、Stage0控制、P6安全健康模拟及新增P7统计/UART/CAN回调均PASS（40项原生编译/运行/负向检查）；Python离线13项PASS，包含无设备/缺依赖落盘和模拟串口全流程；正式Keil Stage0 Rebuild/有效宏/预处理/MAP/HEX均PASS。明细与可复制命令见[P07_HANDOFF](P07_HANDOFF.md)。

G-B、T1-A、T1-B、T2真实串口/CAN、真实stack HWM及P6旧硬件证据复核均NOT_RUN，数字待测。旧P6b交接转述的P6a“测试完成”仅为USER_REPORTED，逐项R不能升级为PASS。G-C/Stage2未授权。主机线程锁模型不替代目标机调度/中断时序。原P2b/P3b旧全量脚本（含过时按键和Stage1路径）未原样运行，由P7复用适配入口记录当前源码真实结果。

## 0. 状态、权限、证据分级

- `PASS`：已真实执行且输出证明满足该测试断言。
- `FAIL`：实测或主机执行未满足要求；保留日志。
- `NOT_RUN`：没有执行（包含无Keil/板卡/授权）。
- `USER_REPORTED`：用户说明曾实测但无本轮证据，须继续追溯日志或记录。
- `NOT_APPLICABLE`：该工况不适用，不等同PASS。

所有新实机测试**在Codex提交可审查固件、用户确认接线/烧录方式之后**执行。正式Stage0无PWM，不进行带载动作。

## G-A 姿态映射确定性验证（Codex本机必做）

| Case | 步骤 | 断言/处理 |
|---|---|---|
| GA-01 | IMU有效静止100连续样本/中途移动中断 | only valid windows calibrate、运动打断重新累计 |
| GA-02 | 授权初始Roll/Pitch near horizontal | PWM目标1500/1500us |
| GA-03 | ±1.9° / ±2° / +5° | 在死区目标回零（可能经2us/update逐渐回零）；+5°稳态目标偏移15us |
| GA-04 | ±20° | 限至±15°映射、目标1455..1545us |
| GA-05 | 1500→1530目标跃迁 | 每次目标更新差≤2us，完整步序正确 |
| GA-06 | NaN/Inf/过期IMU样本/重复generation | 按当前策略拒绝，不使旧命令重放 |
| GA-07 | LOCAL/REMOTE/STOP/失去授权 | 每个控制源独立且互斥，失败路径不继续Apply |

## G-B Stage0真实IMU输入（用户）

启动后静止约2秒完成标定；OLED IMU页观察Roll/Pitch 30秒稳定显示；分别缓慢左右滚转/前后俯仰各3轮并回中，记录相应字段变化与状态。无角度参考仪器不可声明绝对0.1°精度、机械闭环追踪或响应延时。

## G-C Stage1无负载PWM（**独立权限门禁**）

只在再次确认后执行：确认 `P3B_OUTPUT_STAGE=1`，舵机**供电和两根信号线均断开**，逻辑分析仪 PC6/PC7/GND，采样率至少1MHz（推荐≥2MHz），P6授权状态允许输出。先核对 `HEALTH?`；MANUAL需新合法MODE LOCAL，LOCK/EMPTY/BAD禁止绕过。依当前KEY0=STOP，KEY1=翻页，不用旧文档启动指引。验证初始静止、中心、≥40ms priming、约50Hz、Roll→PC6/Pitch→PC7、1455..1545us、2us/控制更新、CAN STOP/KEY0禁用。**没有Stage1授权不运行，不影响已批准Stage0 P7交付。**

## T1-A Stage0基准120秒（用户）

- 开机后一律保持安全页、固定健康策略；上电10秒预热以后确认PERF2 n>0，记录开始`HEALTH?`/`COUNT?`/`PERF?`/`PERF2?`/`CANC?`。
- 留全部任务运行：Control20ms、IMU10ms、ADC DMA、Sensor500ms、Display、Health100ms。CAN没有持续外部发送，UART5秒读一次诊断，自动ADC报告保持现状。
- 运行120s，记录最终周期min/max/采样数/worst elapsed/违例、Control/Idle/Health剩余栈、故障与是否重启。T1-A结果与T1-B时间窗口不等长的偏差写入报告。

## T1-B + T2 600秒联测（用户）

- 建议独立安全重启进入同一Stage0和相同10秒预热（**绝不全片擦除安全页**），确认HEALTH正常后读基线；从PC/RK两端近同时运行脚本，并记录实际起止区间。
- RK只发送原`0x183`合法heartbeat，目标50帧/s、600s（名义约30000条）。PC每秒10条PING、持续600s并每5秒取PERF/PERF2/CANC/COUNT，保留ADC自动行分类。
- RK发送结束后等待CAN队列短暂排空（例如1秒，不再发送负载），PC再读CANC终值以消除剩余在途出队误差。记录can0错误与bus-off，若发现异常保留现场日志。
- 核验周期范围19000–21000us、窗口独立违例=0、最大elapsed<2000us、无意外IWDG/Control fault、关键栈剩余>0、UART PONG正确率、UART OVF事件、CAN已处理数量和检测到的queue overrun事件。任何不满足保留真实数据，不许重新定义口径。
- 注：主机单调时钟不共享基准，因此**不能宣称严格同步起止**；只报告近同时的共同负载工况及实际各端日志。

## T2短测（用户）

- `T2-U`：分段发送`PING`（故意分两次USB写），混合LF/CRLF及COUNT?，30秒累计输入>256字节跨DMA circular回绕，速率不过载；PONG分类、RX增长/OVF准确。不能在主机脚本里出现SAFE CLEAR或控制授权指令。
- `T2-C`：Stage0约30秒CAN MODE/CONTROL/STOP烟测（只验证协议解析/模式变迁/STOP，不应输出PWM）；动态epoch/action/seq以实际STATUS为准；原`doc/P3b/rk3588_test.py remote`模式在Stage0会因不能ACTIVE而报错，**不要用它替代Stage0专用烟测**。如需新脚本，允许在`p7_can.py`增加显式`smoke`子命令，但默认`load`永远只发heartbeat；不允许自动动作或循环重启。

## R P6复用（用户/资料）

查验真实测试回执：健康RUN+WDG ON；真正IWDG重启（R=IWDG）；L=1/P=LOCK；普通旧控制拒绝；如已做NRST/POR仍保锁；两步恢复MANUAL且A=0。没有回执则记录USER_REPORTED/NOT_VERIFIED，不重新擦写Flash安全页，不随便再次故障注入。不能说“精确2秒IWDG复位”。

## M正式Keil/MAP与stack（Codex/用户）

记录正确Stage0下 Code/RO/RW/ZI、Flash=Code+RO+RW、RAM=RW+ZI、ARMCC版本、BUILD PASS/FAIL、记录安全页隔离；读取Control/Idle/Health栈高水位(words)。无足够数据不编造CPU利用率或其他任务高水位。

## 总验收标准

- G-A host PASS、G-B实机输入记录、T1-A/B与T2具有真实数据、P6证据状态明晰、正式Stage0 Keil PASS、资源数据更新、无新增安全回归。
- G-C没有独立批准可仍记录`NOT_RUN`，但简历只能写算法映射/输入，不写实际电气输出已测；Stage2始终不在本次授权。
- 有结果不达标也如实保留、评估是否做一次同负载有针对性优化；不得为漂亮简历数字改变控制门限。
