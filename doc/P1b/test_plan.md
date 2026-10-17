# P1b 实测与验收（无 SWD）

## 基线

记录真实 local HEAD、工作树 diff、Keil target、ARMCC version、编译宏、Build Log、MAP/AXF/HEX、日期、固件 SHA-256、逻辑分析仪型号/采样率/阈值。用户已报告 P1a Case1–3 波形符合预期，不重做；机械 H10 还须单独报告。

## 测试表

| ID | 操作 | 通过标准 / 必留证据 |
|---|---|---|
| B01 | 关闭 P1b test、复位无舵机 | 启动与调度存在；PC6/PC7 无未经授权 PWM；默认模式不会输出 |
| B02 | FreeRTOS 正常启动 | 唯一 RVDS CM3 port、`xTaskCreateStatic`/Scheduler 不失败；真实构建、上板可观察结果 |
| B03 | Tick 中断路由 | SysTick+HAL_IncTick 不双计数/丢 Tick；SVC/PendSV 正常，不进误报 Fatal |
| B04 | ControlTask 唤醒 | **独立 trace** >=1000 个连续周期；20ms 目标；记录 min/avg/max、Jitter/失约次数/负载；建议预设 19–21ms 最小负载阈值，未批准不能事后调整 |
| B05 | 最坏观测执行时间 | trace 高脉宽或外部读出的 DWT；建议候选 <2ms，记录样本数量、测量分辨率/ISR干扰 |
| B06 | PWM 输出/独占 | 有界测试使能双路 1500±10µs，safe小步变更；只有 ControlTask 允许 Apply；LA测双路、无异常窄脉冲 |
| B07 | 故障/超时/延迟注入 | 构造受控任务迟到/过期命令后禁止 PWM，不恢复旧 generation；退出测试/异常路径保持低 |
| B08 | 静态内存/Stack | Build MAP Flash/RAM 数字、static 控制任务 TCB、Idle TCB/stack，uxTaskGetStackHighWaterMark（单位 words）真正报告；未测不得 PASS |
| B09 | 模式与重复授权 | 正常固件复位默认 DISABLED；受控测试结束 Disable、不自动恢复；故障锁存后普通调用无法重启 |
| B10 | 适用实机测试 | 仅 P1a H10 通过且 B01–B09 无负载证据满足后，授权受限机械；记录安全动作与故障行为 |

## 无调试器测量落地

用户有 CH340 串口下载 + 逻辑分析仪，没有 SWD。PWM 引脚只能证明 Timer PWM 行为，**不能证明 ControlTask 真实每 20ms 唤醒**。在用户批准一根经板级审查不冲突的 TRACE GPIO 前，不得假定其存在；LA CH0 捕获 TRACE，另一次捕获 PC6/PC7。若只用 PC6/PC7，B04/B05 填 NOT_RUN。

## 候选预批准阈值

- ControlTask wake 间隔：标称20ms，最小任务负载候选 [19,21]ms；不允许无界调度延误；至少1000个样本。
- Execution duration：候选 <2ms（须明确 marker 包含哪段代码），记录最坏观测值；测量完成后不反向调整。
- PWM：P1a 基线 50Hz、周期 19900–20100µs、脉宽目标±10µs；稳定波形采样≥100周期。
- SRAM/Flash/栈余量：记录实际数值，阈值由本项目静态预算评审批准，不编造。

## 异常/暂停

意外 PWM、输出不禁用、SVC/PendSV 进入 Fatal、HardFault/clock fail、错误 APB/Timer 寄存器、失约周期持续出现、栈不足或输出恢复旧命令，停止带载测试。不要通过屏蔽 configASSERT 或修改容差掩盖。

## 记录模板

```text
Test ID / Date / local HEAD / dirty diff:
Keil ARMCC version / target / Build log / MAP / firmware hash:
P1a H10 outcome:
Hardware (STM32 board / servo supply, ground):
Logic analyzer model / sample rate / thresholds / trace pin approval:
Test firmware macros / stimulus / expected sequence:
N / period min-avg-max / jitter definition:
Worst execution time / timing reference:
PWM CH1/CH2 widths / enable-disable transients:
Flash, RAM, ControlTask/Idle stack residual (units):
Output / CSV / photos file path:
PASS / FAIL / NOT_RUN + rationale:
```

## C2 交付执行入口（2026-10-17）

- 实际无 SWD、无需 UART 输入步骤见 `C2_hardware_guide.md`；本轮用户选择不新增测量引脚，B04/B05 和关断时延均 NOT_RUN。
- `C2_build.ps1` 使用同一 uvprojx/target 顺序构建4个受控无舵机镜像，再按字节恢复未授权工程并构建默认镜像。没有烧录动作；每份 Build Log/MAP/HEX/AXF/宏和哈希在 `evidence/C2/{default,case1,case2,case3,case4}/`。
- `C2_host_test.py` 编译并执行真实 control/policy/test-source C，硬件与 RTOS 是显式 stub，只验证决策与调用顺序，不验证真实 FreeRTOS、PWM、Tick、DWT或栈余量。
- `C2_verify.py` 核对全部真实 ARMCC 镜像的向量、静态分配、默认输出代码缺席、保护文件哈希和11项负向编译授权检查。旧 `C1_verify.py` 是历史 C1 专用，不应用来验证已进入 C2 的工作树。
