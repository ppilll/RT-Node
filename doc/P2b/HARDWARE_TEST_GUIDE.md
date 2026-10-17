# P2b 操作者无调试器测试指南

**现阶段先不带舵机**。仅在 P2a关键前置证据完成、Stage1无负载测试通过之后，才可研究 Stage2 带载。没有 SWD/J-Link/示波器/UART已验证应用日志。

本轮软件/Stage0与Stage1 Rebuild已完成，**未烧录，H01~H04全部NOT_RUN，实物KEY电平/接线NOT_VERIFIED，G2未批准，Stage2未构建**。Stage1 HEX仅完成构建，不表示G1烧录门禁已通过；默认工程仍Stage0。

## 1. 准备

1. 保存现有P2a原始 SPI抓取和H01~H04现场记录；只看9.8ms周期不足以证明完整健康。记录P2b Hex SHA256。确认板载KEY0/KEY1位置与GPIO资源；若PE4/PE3无效，禁止使用臆测接线的输出镜像。
2. 舵机信号/独立电源物理断开；IMU继续用 SPI1 PA5 PA6 PA7 / CS PC4 / 3.3V / 共地。
3. Stage0 默认镜像`evidence/armcc/stage0/RTnode.hex`：上电 PC6/PC7 只能静态低电平；按KEY0也不应输出PWM。
4. G1条件及无负载操作者授权落实后才烧录`evidence/armcc/stage1/RTnode.hex`，核对[P2b_HANDOFF.md](P2b_HANDOFF.md)的SHA256，重置BOOT为正常应用启动。确保 KEY0在上电时松开，KEY1未按。初始IMU水平静止，至少2秒，让P2a校准完成。若实际引脚/电平不符或有外接占用，停在Stage0，不猜替代引脚。

## 2. LA测量 H01/H02（不插舵机）

- CH0=PC6、CH1=PC7，GND=MCU GND；同时需要可用键输入证据时可选CH2=PE4、CH3=PE3；若与SPI不能同时测，分阶段记录。
- 不按KEY0观察至少3~5秒：两条PWM线静态低。上电长按KEY0也不能自动使能（需要松开后新按）。
- READY后先松开再按KEY0，每次保持≥100ms以覆盖两次采样去抖。授权后先观察PWM首次启动保护帧与1500µs中位；首次Apply不得早于Enable后40ms；20ms输出周期（50Hz）。无独立软件读出时，不能仅凭中位脉宽不变反推精确Apply时刻，该时刻仍NOT_MEASURED。
- 从使能时的手持参考姿态，缓慢左右倾、前后倾，查两个通道各自响应、方向、脉宽均在1455..1545 µs的初始小范围内；任意相邻20ms更新的目标变化最大±2µs（注意实际PWM占空变化可能隔帧才见，记录相邻稳定脉宽）。
- 按 KEY1：输出应禁用（PC6/7恢复低）；松开再按KEY0不能重启该会话。复位后需要重新显式授权。
- 输出后若突然出现异常停机，先保持舵机断开，保存捕获，不要绕过fault latch。

## 3. H03：输入缺失与安全关闭

- 断电移除IMU模块，再上电、尝试 KEY0：不得产生PWM；用SPI和PWM分别取证。不要热插拔。
- 若需验证“运行中失效”的关闭，优先选已在主机侧验证的受控注入镜像，确保连接舵机前独立审查。没有批准注入条件就标 NOT_RUN，不直接短接信号或在带载时拔IMU。

## 4. H04：真实舵机小范围运行（仅G2通过后）

- 确认机械活动空间、独立舵机供电满足额定值、MCU/舵机地共地；不要让舵机由MCU 3.3V输出引脚供电。
- 上电未授权无运动；IMU水平静止校准后按KEY0，仅在设定±45µs附近运动。先单轴确认，观察正反方向、晃动和机械硬限位；按KEY1确认立即进入停输出状态。
- 如发现运动方向错误，改配置方向常量后 **先重复Stage1无负载**，再带载。不为追求活动幅度扩大Pan/Tilt软限制。
- 注意：停止 PWM 不保证所有种类的舵机立刻无保持力或机械制动；KEY1不是认证急停，保留断独立电源能力。

## 5. 证据

`doc/P2b/evidence/hardware/` 存原始LA抓取、CSV、截图、Test ID、HEX hash、编译宏、测量条件与人工结果。主机PASS不等于LA/机械PASS；无运行trace时保持 WCET/栈余量 NOT_MEASURED。

## 6. 复现构建（仓库根PowerShell，不烧录）

```powershell
python doc/P2b/run_host_tests.py
./doc/P2b/rebuild.ps1 -Stage 0
./doc/P2b/rebuild.ps1 -Stage 1
python doc/P2b/check_static.py
```

Rebuild脚本恢复工程默认Stage0及原已有输出，交付HEX只在evidence目录。Stage1临时追加`P2B_OUTPUT_STAGE=1,P2B_UNLOADED_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL`，P1A/P1B仍0。Stage2要求独立`P2B_OUTPUT_STAGE=2,P2B_G2_EVIDENCE_ACK=1,P2B_OPERATOR_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL`且UNLOADED_ACK=0；本轮没有G2批准，不构建。编译宏不替代证据或手动KEY0。
