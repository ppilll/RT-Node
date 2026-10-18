# RT-Node F103 — P4 Codex 文档包 V1.1

批准基线：2026-10-18，GitHub `ppilll/RT-Node` 主分支 `7bb967a3b8c47b63bd4afdc44feabba0c4e4bf00`（P3b）。Codex 开始时读取当前最新源码；如代码已变化，以实际源码为准并在交接中注明，不重复做哈希审计。

## 使用方式

1. 把本包 `doc/P4a/`、`doc/P4b/` 复制到 RT-Node 仓库相应目录。先阅读 `doc/P4a/README.md`，在 **一个 Codex 对话** 中按 `doc/P4a/codex_tasks.md` 实施 P4a。
2. P4a 的 Keil 编译和 T1 串口实机结果确认后，生成 `doc/P4a/P4a_HANDOFF.md`，**不要自行宣称 PASS**。
3. 新开一个 Codex 对话，阅读 `doc/P4b/README.md`，在 P4a 稳定接口之上实施 P4b；完成 Keil 编译、T2/T3 实机后生成 `doc/P4b/P4b_HANDOFF.md`。
4. 只有 P4a/P4b 两个子阶段有实际 PASS 结果后，提交精简 `doc/P04_HANDOFF.md` 给总控审核是否 P4 ACCEPTED；**禁止自动进入 P5**。

可直接复制到 Codex App 的首条消息：`CODEX_START.md`。

## 非目标

本包仅规定实施方案，并未修改固件、运行 Keil 或完成实机测试。不得把包内预计结果写成已完成。不要改动 ControlTask / CanTask / ImuTask、CAN 协议、TIM3 舵机 PWM、LOCAL_FOLLOW、执行器归属或 STOP 逻辑；不要建立额外中间件、日志系统、审计体系、测试开关。最多三组实机测试：T1/T2/T3。

## 硬件事实与约束

- 正点原子精英 V2，STM32F103ZET6，72 MHz，512KB Flash / 64KB SRAM；Keil5、ARMCC 5.06u7、HAL、FreeRTOS 静态对象、1 kHz Tick。
- P4a：USART1 PA9/PA10，经板载 CH340；用户采用**手动 BOOT0+RESET** 下载，已确认运行后同一个 CH340 可正常串口通信。
- P4b：ADC1_IN1 = PA1；**PA7 属于 ICM20602 的 SPI1 MOSI，不得动**。板子 **P7 接口** 不是 PA7；其 STM_ADC/TPAD/RV1 跳线状态尚待实物确认，接线安全核对后才能做 T2。
- 用户仅有逻辑分析仪和 CH340，没有 ST-Link/J-Link/SWD Watch/ITM/示波器/万用表。

## 资料（仅供查证；最终以仓库自带 HAL 为准）

- 仓库：<https://github.com/ppilll/RT-Node>
- RM0008：<https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- STM32F1 HAL 上游：<https://github.com/STMicroelectronics/stm32f1xx-hal-driver>
- F103 TIM CC 触发细节参考：<https://community.st.com/stm32-mcus-products-25/triggering-adc-1-with-timer-1-stm32f103-95975>

## 必要验收原则

Codex 可以实施、静态核查并执行本机可用的 Keil 编译，但**硬件测试必须由用户完成并返回实测记录**；没有测试记录则记为 PENDING。P3 文档未同步不阻塞 P4。不要额外实施 P1–P3 回归测试。
