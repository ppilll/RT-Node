# P3a 精简测试 T1（V1.0）

前置：用户确认 CAN 收发器/终端/共地及历史 bitrate；舵机电源与信号断开。Keil5/ARM Compiler 5.06u7 编译，正常串口下载；无 ST-Link/J-Link，不依赖 Watch/SWO。

## 唯一必要的整合测试：T1 CAN 双向通信

在 RK3588 Linux（把 `can0` 换成真实接口，`<RATE>` 换成用户确认的值，不要直接复制尖括号到 shell）：

```sh
ip -details link show can0
sudo ip link set can0 down
sudo ip link set can0 type can bitrate <RATE>
sudo ip link set can0 up
candump can0
```

发送合法心跳：

```sh
cansend can0 183#0100010000000000
```

期望：无致命故障时周期 `0x280` STATUS（DLC=8），P3a `state=0`，`epoch=0`，`actuator_enabled` bit0=0；合法新 heartbeat 到达后 bit2=1（500ms 内观察），IMU bit1 可能随实际状态变化，不预设为0/1。

发送模式请求，必须拒绝：

```sh
cansend can0 181#0102010000000000
```

期望 STATUS B2=`0x05 REQUEST_REJECTED`，B6..7=`01 00`，flags bit5=0，epoch仍0。发送 STOP 测幂等反馈：

```sh
cansend can0 182#0101020000000000
```

期望 STATUS B2=`0x02 REMOTE_STOP`，B6..7=`02 00`，flags bit5=1，仍 `state=DISABLED` 且不使能 PWM。可发送协议格式错误帧（例如短 DLC）一次，确认不触发模式/输出，避免扩展成大量负测。

P3a 若见到 `REMOTE_CONTROL`（例如 `180#01000100DC05DC05`），**不得开启舵机或改变状态**。检查测试期间 PC6/PC7 无有效 PWM，默认禁用；若尚未接逻辑分析仪，可记录断开执行器且无使能代码路径的事实，但不可声称完成 PWM 实测。

## 通过准则

- 编译确实成功；真实 STM32/RK3588 收发成功；Standard ID、DLC、payload、STATUS 语义正确。
- 无 CAN 使能执行器路径，输出仍处于 P3a 禁用状态。
- 保留少量 candump 文本、必要的逻辑分析仪截图；不重做 P1/P2 既有压力或全量时序测试。

## 记录模板

| 检查 | 实际证据 | PASS / FAIL / NOT_TESTED |
|---|---|---|
| Keil 编译 | [日志/结果] | NOT_TESTED |
| RK3588→STM32 合法心跳 | [candump] | NOT_TESTED |
| STM32→RK3588 STATUS | [candump] | NOT_TESTED |
| MODE_REQUEST 拒绝反馈 | [candump] | NOT_TESTED |
| STOP 空操作幂等反馈 | [candump] | NOT_TESTED |
| 未使能 PWM（必要验证） | [截图或代码路径证明说明] | NOT_TESTED |

**注意**：candump 可能显示 RK3588 本机发送帧回显；不能将回显当作 STM32 已收到。必须确认由 STM32 发出的 `0x280` STATUS 才能证明双向链路。
