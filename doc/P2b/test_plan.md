> **P7 安全更正（2026-10-19）：本页是历史 P2b/P3b 设计/测试记录，按键授权步骤不适用于当前 P5/P6 源码。当前 KEY0(PE4)=STOP 且锁存至复位，KEY1(PE3)=OLED 翻页；不得按 KEY0 启动、不得用 KEY1 停机。控制授权服从 P6 持久锁和现有 MODE 策略。Stage1/Stage2 本轮未授权，当前操作以 [P7 现场指南](../P7/operator_runbook.md) 为准。**

# P2b 最小测试计划（不做过度防御性测试）

| ID | 类别 | 必须确认的现象 | Gate |
|---|---|---|---|
| S01 | STATIC | Actuator、核心异常/时钟、P2a IMU 不变；只有 ControlTask 正常写 PWM；默认编译 Stage0。 | G0 |
| B01 | BUILD | Keil ARMCC5 工程完整编译；Stage0 0 error；Stage1 受控构建成功；产物HEX及hash/资源记录。 | G0/G1 |
| U01 | HOST | 映射：初始1500、±Roll→Pan、±Pitch→Tilt、死区、±15°输入、±45µs输出、±2µs/周期限速、非有限数。 | G0 |
| U02 | HOST | ControlSession TEST历史行为未变；LOCAL_IMU受明确来源授权；TTL40边界、代际重复与回绕、P1安全限位及 terminal。 | G0 |
| U03 | HOST | 无按键无PWM、boot长按KEY0不arm、READY后新按键、KEY1停止、传感器失效/过期关闭、无自动恢复、40ms priming。使用真实控制逻辑与HAL/RTOS stubs，不得只验证假状态机。 | G0 |
| H01 | LA无负载 | P2b Stage1上电 KEY0不按期间 PC6/PC7静态低；KEY0后先低帧/中位，再开始有限变化；KEY1关闭。 | G1 |
| H02 | LA无负载 | Roll/Pitch手持倾斜 → PC6/PC7预期方向与幅度，20ms PWM，限幅/速率，无负载。 | G1 |
| H03 | LA无负载 | 断电移除IMU再上电，无法授权输出；或在已使能时使用经批准的安全故障注入替代方案，证明失效关闭。绝不带载热插拔。 | G1 |
| H04 | 机械受限 | G2后独立供电、共地、单轴小幅运动、KEY1停止、机械不触限；记录人工观测。 | G2 |

原则：P2b HOST 与 Build 由 Codex 执行和存证；H01~H04 由操作者执行。仅 LA截图无解码时不得推断数值。没有角度基准不得宣称绝对姿态精度。无独立 GPIO Trace、SWD 读出时真实 ControlTask WCET/栈余量记 NOT_MEASURED。初次只做以上最小项目。

### 单项记录模板

```text
ID：
commit/HEX SHA256：
固件stage/全部授权宏：
日期/操作者/硬件接线：
工具/LA型号/采样率/阈值：
输入与预期：
实际观察与量测：
原始capture/CSV/图片：
状态：PASS / FAIL / NOT_RUN
局限与整改：
```

### 验收

P2b `ACCEPTED` 需 S01/B01/U01~U03、H01~H04 全部真实 PASS，以及 P2a关键前置证据G2获批准；否则 `PARTIAL`。有限真实运转成功不代表取得了未测的CPU/WCET和系统整体可靠性证据。不得自行进入P3。
