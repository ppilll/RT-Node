> **P7 安全更正（2026-10-19）：本页是历史 P2b/P3b 设计/测试记录，按键授权步骤不适用于当前 P5/P6 源码。当前 KEY0(PE4)=STOP 且锁存至复位，KEY1(PE3)=OLED 翻页；不得按 KEY0 启动、不得用 KEY1 停机。控制授权服从 P6 持久锁和现有 MODE 策略。Stage1/Stage2 本轮未授权，当前操作以 [P7 现场指南](../P7/operator_runbook.md) 为准。**

# Stage2构建交付 — 2026-10-17

**Stage2真实ARMCC5 Rebuild通过；未烧录；Stage2机械带载测试NOT_RUN。**

## 授权及证据来源

- 源码HEAD：`59e4a0b0ebf738a8221f719703948e798c6d7d47`，main；进入时工作区干净，无生效AGENTS.md。
- 操作者原话：“Stage1测试通过，帮我解除限制，编译Stage2。”
- 对P2a身份/配置、姿态数据、失效关闭证据评审，以及方向/机械范围/独立供电/共地/现场断电办法的G2确认，操作者回复：“这些G2条件均已确认通过”。本次请求作为Stage2明确编译批准。
- G1/G2记为操作者报告/确认通过，未提供本轮原始capture供Codex独立复核；不覆盖历史硬件NOT_RUN，不伪造测试数据。Stage2实际带载H04、Codex硬件操作均NOT_RUN。

## Stage2主要做什么

接入真实舵机，在中位附近验证受限Roll→Pan、Pitch→Tilt跟随，确认实际机械方向、平顺性、活动空间及KEY1停止。控制代码与Stage1相同，Stage2提供独立的带载试验编译许可，不扩大运动范围，不增加Yaw或新任务，不进入P3。

仍保持目标1455..1545µs、每20ticks最多变化2µs、原驱动安全范围/故障锁存、KEY0一次人工授权、KEY1停止后本次启动不可重新授权、READY/valid/fresh/新generation检查，以及Enable后≥40ms双时钟priming/UIF检查。

## 真实构建

```powershell
./doc/P2b/rebuild.ps1 -Stage 2 -G2EvidenceAck -OperatorAck
```

- Keil Target 1，ARMCC5.06 update7 build960；完整日志`evidence/armcc/stage2/rebuild.log`。
- 时间：2026-10-17 14:56（Asia/Shanghai），退出码0，**0 errors / 0 warnings**。
- 宏：P1A_ACTUATOR_TEST_ENABLE=0、P1A_AUTOTEST_ENABLE=0、P1A_AUTOTEST_CASE=1、P1B_AUTOTEST_ENABLE=0、STM32F103xE、USE_HAL_DRIVER，以及`P2B_OUTPUT_STAGE=2,P2B_G2_EVIDENCE_ACK=1,P2B_OPERATOR_ACK=1,P2B_OUTPUT_ARM_KEY=0xB2201500UL`；UNLOADED_ACK默认0。
- Code/RO/RW/ZI：16162 / 382 / 112 / 5800 bytes；Flash16656 bytes、静态RAM5912 bytes。
- HEX：`evidence/armcc/stage2/RTnode.hex`。
- HEX SHA256：`711e57eb71ddc83470f5a62cd5a0cdb35872f23a0a211afcbf537b7b162eaff1`。
- MAP SHA256：`d4a92b2790b0c4291e58888e936184a05355d81ba42a84399366e5116c1e2777`。
- AXF SHA256：`c04f323f8e6bd005b79b1cbe0a5a7ceb655f34e2507bbd133e503b276bbfd4e7`。

Stage1/Stage2的HEX逐字节相同：阶段宏仅在预处理时约束授权组合，两个有效输出阶段选择相同运行控制路径。Stage2为实际重新编译，不是复制Stage1文件；独立日志、build.json、构建工程/宏、版本、AXF/HEX/MAP、callgraph及hashes.json均在`evidence/armcc/stage2/`。

## 本次改动和验证

- 仅扩展`doc/P2b/rebuild.ps1`支持Stage2及两个显式确认开关；更新README、HARDWARE_TEST_GUIDE、P2b_HANDOFF并新增本文件/Stage2构建证据。
- PowerShell语法检查PASS；缺G2/Operator开关的Stage2构建在执行前被拒绝，Stage1携带Stage2确认开关也被拒绝。
- 生产App/Core/Driver/Middlewares和Keil工程相对本轮HEAD不变；构建后工程仍默认Stage0，已有工程输出恢复。核查见`evidence/armcc/stage2/verification.json`。
- 生产控制代码未变且HEX与已验证Stage1相同，本次未重复HOST测试；既有HOST结果保留，不声称重跑。Stage2的真实机械H04及WCET/栈余量分别为NOT_RUN、NOT_MEASURED。

烧录请明确选上述stage2目录的HEX。首次带载先试单轴中位附近运动，并确认KEY1停止；现场操作见[HARDWARE_TEST_GUIDE.md](HARDWARE_TEST_GUIDE.md)。
