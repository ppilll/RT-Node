# P3a_HANDOFF — 2026-10-17 验证固件交付

**状态：BUILD_PASS（用户已批准用于实测的配置）；T1 / 烧录 / PWM实测均 NOT_TESTED；P3a 未 ACCEPTED。**

## 本次配置与修改

- HEAD与设计基线均为 `59e4a0b0ebf738a8221f719703948e798c6d7d47`；未提交/推送。复查无适用AGENTS.md，保留进入前已有P2b文档/Stage2证据改动。
- 本次生产源码只改 `App/can/can_timing.h`：500000bit/s、APB1=36MHz、Prescaler4、BS1=14TQ、BS2=3TQ、SJW1TQ；采样点15/18≈83.333%。Classical CAN / Normal / 标准11bit，PA11 RX/PA12 TX。
- `P3A_CAN_TIMING_USER_APPROVED=1` 表示用户明确批准用于实测；`P3A_CAN_TIMING_CONFIRMED=0` 表示尚无T1通过证据。`P3A_CAN_UNVERIFIED_BUILD_ONLY=0`，本次不使用旧build-only覆盖值。默认配置允许构建验证HEX，不再因缺少历史实测记录阻断。
- RK3588由用户指定为ATK-DLRK3588、can0、rockchip_canfd、99MHz。硬件依据由用户指定为正点原子精英V2/ATK-DLRK3588官方原理图，使用板载CAN收发器/终端；实际跳线、接线、端接由用户现场确认。本轮未操作实机，也不声称独立核验原理图。
- 构建/文档：新增 `rebuild.ps1`，更新 README、T1_PREPARATION、本交接和当前源码diff。旧UNVERIFIED编译记录保留为历史，不能用于本次刷写。
- 现有 `can_app.c/.h`、main、Keil工程自上轮P3a实现后字节未变；冻结协议 `interfaces.md` 未改。ControlSession/ControlTask/LocalFollow、Actuator、IMU、HAL源码/FreeRTOS、异常/时钟/startup保持不变。

## 真实构建与可下载产物

2026-10-17 **16:09（Asia/Shanghai）**，执行：

```powershell
./doc/P3a/rebuild.ps1
```

- Keil5 / Target1 / 实际 **ARM Compiler 5.06 update7 build960**，完整Rebuild与链接 **PASS：退出0，0 errors / 0 warnings**。日志顶部V6.16是现有工具配置别名，真实编译器见版本文件。
- 构建宏保持P1A/P1B输出测试全部0，显式 `P2B_OUTPUT_STAGE=0`；CAN参数直接来自上述头文件，无命令行时序覆盖。
- Code19846 / RO382 / RW156 / ZI7740，Flash20384bytes、静态RAM7896bytes。
- **串口下载HEX：[P3a_CAN500k_STAGE0_T1.hex](evidence/armcc_validation/P3a_CAN500k_STAGE0_T1.hex)**。
- **SHA256：`816CD1BC469F73E3F2864583FAD0AAF50E48ABB3432251ABD7AA8E576CB631AD`**。
- Intel HEX全部记录校验和/EOF检查通过，数据范围 `0x08000000..0x08004F9F`，20384bytes，位于F103ZE主Flash。按既有串口流程下载本次HEX，完成后恢复正常启动设置并复位；Codex本轮未烧录/未验证实际串口连接。
- 构建后已恢复默认工程及既有Projects输出；应明确选择上述独立目录的本次HEX，不能选到P2b旧HEX。

证据：[rebuild.log](evidence/armcc_validation/rebuild.log)、[compiler_version.txt](evidence/armcc_validation/compiler_version.txt)、[build.json](evidence/armcc_validation/build.json)、[MAP](evidence/armcc_validation/P3a_CAN500k_STAGE0_T1.map)、[verification.json](evidence/armcc_validation/verification.json)、[当前源码指纹](evidence/armcc_validation/source_hashes.json)。构建工程审计副本中的相对路径以Projects为基准，复现请使用上面的脚本。

## P3a禁输出边界与静态检查

- 沿用静态8帧队列/CanTask；FIFO0只收0x180..183标准数据帧，所有类型DLC8；RX ISR最多三帧，只有采集/tick/FromISR入队，无协议业务或阻塞发送。scheduler运行后才Start/开启通知，NVIC数值6；ControlTask优先级及预算不改。
- STATUS为0x280/DLC8，周期100ms及事件发送，邮箱满跳过；正常state=0/epoch=0、flags bit0/3=0，真实IMU/心跳/bus-off及原ControlFault诊断保持上轮实现。
- 所有合法MODE_REQUEST均拒绝（reason05/bit5=0），STOP仅安全空操作确认（reason02/bit5=1），heartbeat仅新counter刷新链路位，REMOTE_CONTROL解析后丢弃；无远程命令快照、模式切换或PWM路径。
- CanTask/ISR不调用Actuator/ControlSession。MAP再次证明Enable/Apply未使用节被移除；HAL CAN与queue.c各一份，没有Legacy重复编译。这是源码/链接证据，**无PWM的实机波形仍 NOT_TESTED**。
- 原fatal handler停止调度时不能保证继续STATUS；运行中的原Actuator锁存由高优先级ControlTask下周期体现在诊断中。本轮不新增恢复或故障码。
- 源码diff见 [evidence/source.diff](evidence/source.diff)（当前完整P3a生产修改，相对基线，含新增C/H）；`git diff --check`、patch反向check通过；新构建脚本语法无错误。未新增测试框架，未重复P1/P2测试。

## 待执行T1与停止点

完整命令、逐字段预期及填写位置见 [T1_PREPARATION.md](T1_PREPARATION.md)。用户确认现场接线/端接、断开舵机信号和供电，按既有串口流程烧录上面的HEX，然后执行一次T1：

1. 先保存 `ip -details -statistics link show can0`，再配置500000bit/s、目标sample-point0.833333、FD关闭，并保存驱动实际配置/统计。
2. candump记录STM32发出的0x280，逐项发心跳/模式请求/STOP/REMOTE_CONTROL；动作串行等待STATUS，不能把本机发送回显当双向成功。
3. 检查正常DISABLED/epoch0、bit0/3=0、心跳更新/超时、模式拒绝/STOP确认及PC6/PC7无PWM，保存少量candump/必要LA证据。

| 项目 | 当前真实状态 |
|---|---|
| 用户批准用于T1的参数/可刷写Stage0固件构建/HEX校验 | 已批准 / PASS |
| 已实机验证的位时序/当前RK配置及物理层 | NOT_TESTED，待用户现场确认 |
| 实际串口下载/启动、CAN双向T1、STATUS字节语义 | NOT_TESTED |
| PC6/PC7无PWM实测 | NOT_TESTED |
| 实际ISR耗时、CanTask运行栈余量/WCET影响 | NOT_MEASURED |
| P3a ACCEPTED | 尚未申请/未通过 |

代码与编译工作在此停止，等待用户T1实机双向通信结果。T1真实通过后根据证据申请P3a ACCEPTED；未经用户再次授权，不实施P3b/P4。
