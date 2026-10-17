# P3a T1 — 一组双向通信准备（尚未执行）

状态：**验证固件 BUILD_PASS；T1 NOT_TESTED，等待用户实机执行。** 用户已批准500000bit/s实测配置，已生成 `evidence/armcc_validation/P3a_CAN500k_STAGE0_T1.hex`。旧 `evidence/armcc_unverified/` 只是历史编译验证；P2b HEX也不是本次P3a固件。

本次HEX SHA256：`816CD1BC469F73E3F2864583FAD0AAF50E48ABB3432251ABD7AA8E576CB631AD`。按既有串口下载流程选择此HEX，主Flash从 `0x08000000` 开始；下载完成恢复正常启动设置并复位。本轮未烧录，不代表串口链路已由Codex验证。

用户提供的历史记录：[VisionArm logs/v1_env/network.txt](https://github.com/ppilll/VisionArm/blob/main/logs/v1_env/network.txt)。旧环境为 ATK-DLRK3588、`can0`、`rockchip_canfd`、控制器时钟99MHz，记录为 DOWN/STOPPED，**没有 bitrate**；本轮未独立联网复核，不能代表当前实机。

## 1. 首先只读现状

RK3588 可连接后先执行并保存原始输出：

```sh
ip -details -statistics link show can0
```

确认实际接口、Classical CAN、bitrate、控制器状态/错误计数。确认收发器型号、电压、跳线/使能、两端端接和共地；STM32 PA11=RX、PA12=TX 经收发器连接，USB不能同时占用这两个引脚。舵机信号和供电断开。

本次STM32获批参数：APB1=36MHz、Prescaler4、BS1=14TQ、BS2=3TQ、SJW1TQ；bitrate=`36000000/(4*(1+14+3))=500000`，采样点=`15/18=83.333%`。RK3588的99MHz不用于STM32计算。硬件依据由用户指定为精英V2和ATK-DLRK3588官方原理图，采用板载收发器/终端；实际跳线/接线/端接待用户现场确认，不声称Codex已检查实物或独立核验原理图。

当前集中配置 `P3A_CAN_TIMING_USER_APPROVED=1`、`P3A_CAN_TIMING_CONFIRMED=0`、`P3A_CAN_UNVERIFIED_BUILD_ONLY=0`。已按此状态完成Stage0、P1测试全关的真实Keil Rebuild；没有用“已测确认”绕过门禁。T1通过且证据评审后再更新确认状态与申请ACCEPTED，本次不提前置位。

用户实机确认跳线/接线/端接后，按获批配置设置RK3588；先保存前述只读输出，再执行：

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 sample-point 0.833333 fd off
sudo ip link set can0 up
ip -details -statistics link show can0
```

确认输出bitrate为500000、FD关闭，保存驱动实际采用的sample-point/时序及错误计数。RK3588驱动会按自身时钟求解采样点，不能照搬STM32的BRP/TQ寄存器参数；命令失败则保留错误输出并停止T1，不把失败当配置成功。

## 2. 一次上电内执行 T1

终端 A 全程记录：

```sh
candump -L can0 | tee p3a_t1_candump.log
```

终端 B 逐项发送，**每次动作等到 STM32 的0x280响应后再发下一项**。本机0x180..183回显不能证明 STM32收到。

1. 上电先观察周期100ms STATUS：标准ID `0x280`、DLC8、B0=01，正常B1=00、epoch B4..5=0000、flags bit0/3/6/7=0。IMU bit1依真实 READY/valid/age<40tick，不预设值。实际故障应B1=07、reason=0x80+ControlFault，先记录故障，不继续把它判作正常通过。
2. 发新心跳 `cansend can0 183#0100010000000000`。500ms内 flags bit2=1；间隔约200ms再发同一条，重复counter不能刷新，在首次接收500ms后（加一周期STATUS观察窗口）bit2应为0。再发 `183#0100020000000000`，链路位恢复，状态/epoch/输出位不变。
3. 发 `cansend can0 181#0102010000000000`（REMOTE/action1/epoch0）。期望 reason=05、last_action=0100、bit5=0、state=00、epoch=0000。随后串行发 `181#0100020000000000`（DISABLED/action2）、`181#0101030000000000`（LOCAL/action3），同样拒绝，last_action分别0200/0300。
4. 发 `cansend can0 182#0101040000000000`。期望 reason=02、last_action=0400、bit5=1，state/epoch不变；重复同一帧仍相同，无执行器操作。
5. 发 `cansend can0 180#00000100DC05DC05`（epoch0/seq1/两轴1500），以及原协议示例 `180#01000100DC05DC05`（epoch1）。均不得改变状态/epoch、置bit3或产生PWM；此类型不逐帧ACK。最后发一次短DLC `cansend can0 181#0102`，last_action/bit5/reason不应因此改变（IMU/心跳位随真实时间变化）。
6. 逻辑分析仪接 PC6/PC7/GND，记录整组测试期间两线保持低、无有效PWM；普通逻辑分析仪不要直接接CANH/CANL。只有源码证据而没有波形时，PWM实测仍是 NOT_TESTED。

示例解码：无故障、IMU未就绪、心跳新鲜时，REMOTE/action1拒绝为 `01 00 05 04 00 00 01 00`；STOP/action4为 `01 00 02 24 00 00 04 00`。IMU就绪分别将flags加02；心跳到期减04。以字段为准，不能把这些示例当实收帧。

## 3. 原始记录（全部待操作者回填）

| 项目 | 本轮状态/证据 |
|---|---|
| 用户批准参数的Stage0 Rebuild、HEX/hash | PASS，见armcc_validation构建记录；不代表已测确认 |
| 当前can0配置/统计、收发器、端接/共地 | NOT_TESTED，待用户现场确认 |
| 实际串口烧录/启动 | NOT_TESTED |
| 日期（Asia/Shanghai）、操作者、板卡/接线/仪器 | NOT_TESTED |
| 双向STATUS、ID/DLC/字节、模式拒绝/STOP/心跳/丢弃 | NOT_TESTED，无candump |
| PC6/PC7无PWM | NOT_TESTED，无LA capture |
| CAN ISR耗时、CanTask栈余量/真实时序影响 | NOT_MEASURED，不从构建推算 |

保留一份上述终端输出/candump及必要LA截图即可，不扩展为P1/P2压力回归。代码与编译完成后Codex停止，等待用户T1结果；全部验收真实通过后再申请评审P3a ACCEPTED，不自动进入P3b/P4。
