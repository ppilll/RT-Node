# RT-Node F103 / P6 Codex 实施包 V1.0

**交付日期**：2026-10-18  
**代码基线**：`ppilll/RT-Node` 默认分支，已核查最新 P5 提交 `fc427fa7c0c3a0a3e81b17e75096e1bc00c5f6bf`。若实施时 HEAD 已变化，Codex 必须先审查差异、暂停冲突修改。  
**工程**：STM32F103ZET6 / 正点原子精英 V2 / Keil MDK5 / ARM Compiler 5.06u7 / STM32 HAL / FreeRTOS，72 MHz，1 kHz Tick。  
**阶段目标**：最小健康汇总 + 控制任务进展监督 + IWDG，防止失去控制监督后未经授权恢复运动。

本包是 **Codex 开发指令与设计文档**，不包含已修改的生产固件、已完成的编译结果或实机测试结论。实际代码修改由 Codex 执行；Keil 编译及开发板测试由用户执行。禁止把待办写成已经 PASS。

## 开始方式

1. 新建一个 Codex 对话，粘贴根目录 `P06_CODEX_MASTER_PROMPT.md` 的全部内容，并提供本目录。确认 GitHub 代码可访问。
2. **串行**完成 P6a（安全启动、持久锁定、健康只读接口、本地恢复）；Keil 编译并完成 P6a 精简验证，提交可审查的 P6a 结果。
3. P6a 通过审查后，另开 Codex 对话完成 P6b（HealthTask / IWDG / HEALTH? 扩充）；不得两个 Codex 并行改同一个 `main.c`、`control.c`、`uart_app.c` 或 Keil 工程。
4. 用户用 CH340、PC 串口助手、逻辑分析仪、RK3588 SocketCAN、OLED 完成 T1/T2/T3；不依赖 ST-Link、Keil Watch、SWO、RTT、示波器或万用表。
5. 依次填写 `doc/P6a/P6a_HANDOFF_TEMPLATE.md`、`doc/P6b/P6b_HANDOFF_TEMPLATE.md`、`handoff/P06_HANDOFF_TEMPLATE.md`。

## 文件导航

- `DECISIONS_AND_LIMITS.md` — 已批准的策略、实现约束、未消除的物理风险（**先读**）。
- `P06_CODEX_MASTER_PROMPT.md` — 可直接复制给 Codex 的总启动指令。
- `doc/P6a/README.md`、`design.md`、`interfaces.md`、`implementation_plan.md`、`test_plan.md`、`codex_tasks.md`。
- `doc/P6b/README.md`、`design.md`、`interfaces.md`、`implementation_plan.md`、`test_plan.md`、`codex_tasks.md`。
- `references/source_audit.md` — 已核查的源码入口和已存在的故障策略。
- `references/official_links.md` — STM32 / FreeRTOS 官方链接。

## 定义清楚“严格锁定”的作用域

- **已持久记录的 IWDG 锁定**：必须跨 NRST、软件复位和 POR/PDR 及完全断电保持；任何普通 CAN MODE、CONTROL、IMU READY 或重启都不得自动解除。
- **首次下载及无法辨识的 Flash 状态**：默认禁用，需本地人工初始化/恢复。
- **人工作业**：经本地串口两步确认，安全复位后由早期启动代码写持久记录；故障恢复后即使解锁，也永久改为 `MANUAL_ONLY`（新模式授权必需），不恢复自动 LOCAL。
- **硬件无法消除的窗口**：IWDG 复位发生后、MCU 在启动代码写入 Flash 锁定记录之前若再次完全掉电，且 RCC/BKP 线索消失，纯片内软件不能绝对证明前次 IWDG；这是剩余风险，不可宣称硬实时/工业级绝对安全。如必须覆盖该窗口，应采用持久硬件锁存或所有冷启动都要求人工授权，并另行批准更改 P5 冷启动语义。

## 变更纪律

- 不修改已冻结 CAN 线上 STATUS、ID/DLC/epoch/seq/action，不变更 KEY0 PE4 STOP 锁存及 KEY1 PE3 翻页。
- 不给所有任务加心跳、不创建第二套执行器授权状态机、不做 P7 压测、不创建复杂故障事件总线。
- 原有 ControlTask 唯一正常 PWM 写入者、Actuator 直接安全关闭、ControlSession 终态锁存全部保留。
- 不在运行时为 IWDG/HealthTask 增加阻塞 I/O；Flash 只在 RTOS 启动前执行必要状态追加写入。
- 不默认启用带载舵机测试；当前仓库 `P3B_OUTPUT_STAGE` 头文件默认 0，绝不能暗中改成 1/2。
