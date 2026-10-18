# P6 决策冻结、可证明边界与开放风险

## 已批准决策

1. IWDG 故障重启后 **禁止自动恢复运动**；普通 CAN MODE、CONTROL、IMU READY 或后续系统复位不能清除该锁。
2. 锁跨 POR/PDR、软件复位和 NRST 保持；优先本地人工恢复，必须有单独明确的恢复操作。
3. KEY0 PE4 仍为独立 STOP 锁存，松开不清除；KEY1 PE3 仍为 OLED 翻页；不得重映射。CAN 协议保持 P3 不变。
4. 当前正常未故障状态保留已批准的 P5 自动 LOCAL 启动机会；**第一次部署因预留 Flash 空白而默认锁定，须明确本地初始化**。实际 IWDG 之后的人工作业使设备永久进入 `MANUAL_ONLY`，确保多次复位也不恢复自动 LOCAL。
5. P6 仅实现轻量健康汇总和必要监督；关键业务进入已安全关闭的 FAULT 并非停喂理由。
6. P6a 完成后串行开发 P6b；P6b 以软件启动 IWDG、标称约 2 s 以及 HealthTask 单点条件刷新为初版候选。

## 核心设计判断

### A. 为何不用仅 RCC_CSR 或仅 BKP

RCC_CSR 重置标志用于**本次**复位归因，但不承担跨多个复位的安全锁存；BKP 数据保留受备份域供电（VBAT）影响，不能保证整机完全断电后持续保持。因此 P6a 用专门 Flash 页中的追加式半字记录作持久化状态，BKP 仅用于受控 `SAFE CLEAR` 重启握手。原有 Flash 半字写入必须按 PM0075 并完成读回验证。

### B. Flash 使用范围、状态与完整性

- STM32F103ZET6：主 Flash `0x08000000..0x0807FFFF`（512 KiB），高密度页大小 2 KiB。**候选预留页** `0x0807F800..0x0807FFFF`。
- Keil 现有 IROM `0x08000000`、Size `0x80000`；需改 Size `0x7F800`，编译后确认 HEX 的应用占用永不进入预留页。
- 使用单个预留页，**顺序追加 16-bit 记录**；首版无运行时页擦除，不在中断或 FreeRTOS 启动后操作 Flash。
- 记录编码建议 `AUTO=0x0F0F`、`LOCK=0x3333`、`MANUAL=0x5555`；三者 16 位中都含 8 个 0，任一记录的从 `0xFFFF` 向目标编程的中间值不能等于另一有效编码。写操作仍必须依硬件规则验证。
- 从地址低到高扫描连续记录直到首个 `0xFFFF`。扫描到不认识的半字、记录序列不合法、非连续数据或存储已满，**fail-closed**；不在运行时自动清空或修复异常记录。
- 空白 `0xFFFF...` = `UNPROVISIONED_LOCKED`，**不是** `AUTO`。
- 有效记录中最后一个为 `AUTO`：允许 P5 普通自动 LOCAL；最后一个为 `LOCK`：严格锁；最后一个为 `MANUAL`：允许新的显式本地/远程 MODE，但禁止所有重启自动 LOCAL。
- 首次明确本地初始化，允许从空白追加 `AUTO`；经 IWDG 锁定后的本地恢复只能追加 `MANUAL`，绝不追加 `AUTO`。若其他状态异常，拒绝串口恢复并提示需离线维修/重新刷机。
- IWDG 复位标志优先于任何 BKP 恢复票据：必须先持久追加 `LOCK`（已有 LOCK 时无需重复写），读回证实，再开放正常启动。写失败进入可观测的禁用态，不能解锁。
- 本地人工确认写入一次性 BKP 重启票据；成功发送回复后 `NVIC_SystemReset()`。新启动只在标志明确为软件复位且没有 IWDG/POR/PDR、票据与反码一致、锁定记录可读、票据已先清除的情况下，追加 `AUTO`（仅未初始化）或 `MANUAL`（IWDG 已锁定）。HAL Flash 写仅在调度器启动前执行；若票据失效则保留锁定。
- 用户确认恢复后，若 Flash 程序中途掉电或记录校验失败，继续锁定；不能仅因为发过 `SAFE REBOOT` 就回报成功。

> **极限窗口**：假设 `AUTO` 已持久有效，IWDG 已把 MCU 复位，但程序尚未写入 `LOCK`，此时主/备份电源同时丢失、清掉复位线索，则原先 `AUTO` 仍然有效。单 MCU 内部软件无法证明曾发生 IWDG，不能把此实现描述为覆盖所有电源时序。独立舵机电源及 PWM 悬挂问题亦不由 IWDG/BKP/Flash 消除。

### C. 不变的安全行为

- `P1a_ForceSafeOutput()` 保持 `main()` 第一项安全动作，PC6/PC7 禁用的硬件原语不可后移。
- `P1a_FatalError()` 与 Actuator 的直接 PWM 安全关闭保持先于看门狗复位。
- 正常 CAN STOP、KEY0 STOP、REMOTE 超时、LOCAL IMU 故障、DRIVER/DEADLINE/EXECUTION 致命锁存继续由原 ControlTask 处理，不被健康状态模块替代。
- UART 维护口是低强度**本地操作确认**，并非身份认证；被物理接入或转发的 CH340 主机可模拟命令，不做不真实的“不可伪造人工验证”保证。
- 数据 Flash 页重刷/量产烧录的 `mass erase` 会清空状态，新的空白态进入锁定，不可自动开机运动。对半擦除损坏页不能通过普通 UART 清锁，需有明确的离线救援流程。

## 当前工程关键参数（在实施前逐条复核）

- `CONTROL_PERIOD_TICKS=20`，`CONTROL_MAX_TTL_TICKS=100`，`CONTROL_EXECUTION_US=2000`，ControlTask prio 1，静态栈 256 words。
- IMU/CAN/UART/ENV/Display 均是优先级 0 的静态任务；ADC 由 UartTask 轮询处理，无 AdcTask。
- KEY0 PE4 由 ControlTask；KEY1 PE3 由 DisplayTask。
- `control.c` 启动时 `s_run.boot_local_pending=true`，有独立的 `Control_PostMode`/`P3bCycle` MODE 通路，所以 **仅修改 boot_local_pending 不充分**。
- `P3B_OUTPUT_STAGE=0` 当前为头文件默认值（无 PWM），不得拿模拟状态变化误报为舵机物理输出测试。

## 放行标准

P6a：完成 Flash 预留页保护、启动安全策略、ControlTask 双层门禁、UART 两步恢复和最小实机验证，Keil 可编译。  
P6b：完成 IWDG 单点喂狗、复位原因报告、三组 T1/T2/T3，Keil 编译 + 用户实机结果。  
未提供用户实测记录，不能宣称 ACCEPTED。
