# P4b 交接 — 代码/构建完成，T2/T3 PENDING

日期：2026-10-18。已阅读本目录原有六份文档、P4a 交接及现有 UART 实现。本轮用户明确确认 **P4a 已验收、T1 已完成，仅旧交接未同步**，据此继续 P4b；不把旧文档的 PENDING 当作阻塞，也不补造 T1 详细回显。工作区原有 P4a 改动已保留，未提交 Git。

## 本轮代码变更

| 文件 | P4b 增量 |
| --- | --- |
| `App/adc/adc_app.h/.c`（新增） | ADC1/PA1、DMA1 Ch1、TIM2 CH2、窗口处理及 ADC 独立状态 |
| `App/uart/uart_app.c` | 原 UartTask 调起 ADC、处理窗口、`ADC?`、200ms 自动报告、较长行分段 TX |
| `Core/Src/main.c` | 原四模块及 UART 初始化之后追加 ADC Init；失败由 UART 报告，不调用 Fatal/STOP |
| `Projects/RTnode.uvprojx` | ADC include/分组；加入 app、现有 HAL ADC、ADCEx、RCCEx 源文件 |

RCCEx 源文件确有链接依赖：仓库 `HAL_ADCEx_Calibration_Start()` 调用 `HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC)`，链接 map 已确认。HAL 驱动源码没有修改，也未引入 HAL TIM 源文件。

`App/uart/uart_app.h` 未改。`Control/Imu/Can/Actuator`、TIM3、`interrupts.c`、`FreeRTOSConfig.h`、原 1kHz SysTick 交接均未改；未新增 AdcTask、软件定时器、TX 所有者或通用日志框架。Keil 更新了 `Projects/` 既有 AXF/HEX、map、调用图、依赖与构建日志等正常产物。

## 外设与启动顺序

- ADC1 regular 单通道 IN1，PA1 始终 `GPIO_MODE_ANALOG`，12bit、右对齐、扫描/连续/间断转换关闭；71.5 周期采样，ADC clock=PCLK2/6=12MHz。
- EXTSEL 使用 **`ADC_EXTERNALTRIGCONV_T2_CC2`**。TIM2 输入 72MHz，PSC=71、ARR=999、CCR2=500，CH2 PWM2（OC2M=111）、active-high、CC2E=1，设计触发率约 1kHz。没有配置 PA1 AF 输出、AFIO 重映射、TIM2 IRQ/DMA 或 TIM2_TRGO。
- DMA1 Channel1：peripheral-to-memory，peripheral/memory **halfword**，memory increment，circular，静态 `volatile uint16_t adc_dma[64]`。map 确认 DMA buffer 为 **128B**；只在传址给 HAL 时转为 `uint32_t *`。IRQ 优先级 6，唯一强定义在 `adc_app.c`，调用 `HAL_DMA_IRQHandler`；不整体复位 DMA1，UART Ch5 保留。
- main 中设置时钟/GPIO/DMA/ADC并校准，TIM2 停止、Ch1 NVIC 关闭；调度器运行后 UartTask 先启动 UART RX，再 `HAL_ADC_Start_DMA`，检查 DMA BUSY/EN 和 ADC DMA/EXTTRIG，最后启用 TIM2 CEN。保留原 HAL/RTOS tick 交接。

## 数据所有权、输出与失败行为

HT 仅置待办标志；TC 仅累计圈数并置待办标志，ISR 不复制/求和/打印。UartTask 沿用最多等待 5 ticks 的检查，发送长 ADC 行时每段不超过 30B、段间再次处理 ADC。HAL TX 每段仍用原 3ms 超时并可被 ControlTask 抢占；实际墙钟延迟受任务调度影响，不声称已实测最大调度延迟。

处理时由 TC 圈数、pending TC、NDTR 得到累计生产进度，选择最新完整 32 样本半窗；较旧窗口计入 `missed`。短临界区内复制 32 个 halfword，**复制后再次检查 DMA 进度与错误标志**，若 DMA 已写回该半窗则丢弃。DMA 不受 NVIC 屏蔽控制，不能单凭临界区认定数据安全。副本求和/除法在临界区外执行；`raw` 为窗口末样本，`avg` 为整数平均，`mv=avg*3300/4095`，`n` 每接受一窗加 32。计数按 uint32 自然回绕。

与现有 UART 环形进度相同，DMA 粘滞 TC 标志不能表示无限积压：ADC Ch1 IRQ 必须至少每 64ms 服务一次。新增临界区只复制 64B，无等待/TX；未做长时间屏蔽中断的承诺。

`ADC?`（实际 LF 结束）及约每 200ms 的自动行格式为 `ADC: raw=<值>,avg=<值>,mv=<值>,n=<值>,missed=<值>` + CRLF。尚无有效窗口返回 `ADC NOT READY`。初始化/启动/DMA/连续 200 ticks 无生产进度分别报告 `ADC ERROR INIT/START/DMA/STALLED`，停用 ADC 自有采样资源，不影响控制仲裁。UartTask 仍为优先级 0、384 words 栈，PING/COUNT/原 RX 协议不变；自动行可穿插命令回复。

`mv` 仅基于 **假设 VDDA=3300mV** 的估算，没有电压精度实测。寄存器计算的 1kHz 不等于实测定时精度。

## 实际 Keil 结果

- 本轮仅运行一次正常构建：`D:/MKD5.36/UV4/UV4.exe -b <工程绝对路径> -t "Target 1" -o <Projects/P4b_build.log绝对路径> -j0`，进程退出码 **0**，日志耗时 **7 秒**。
- 工程 `Projects/RTnode.uvprojx`，**0 Error(s), 0 Warning(s)**，已生成 `Projects/RTnode.hex`。
- **ARMCC 5.06 update 7 (build 960)**，由 `armcc --vsn` 及 `fromelf --text -v Projects/adc_app.o` 的 ArmCC 对象记录确认。uVision 日志首行注册名称虽显示 V6.16，实际工具目录为 ARMCC/Bin，不能据首行误报 ARMCLANG。
- Code=**29886**，RO-data=**382**，RW-data=**208**，ZI-data=**10224**；相对 P4a 交接的构建统计分别增加 3380/0/24/264B。
- 链接调用图：UartTask 可解析调用链 Max Depth=**208B**（含 SendReply → AdcApp_Process），小于配置的 **1536B** 栈；静态调用图不代替实机栈/抢占验证，原私有栈低水位采样保留。
- 构建日志 `Projects/P4b_build.log` 在本机保留，受现有 `*.log` 忽略规则影响；本文件记录了关键结果。源码/工程差异空白检查通过；Keil 自动生成的 `.dep` 等文件自带 CR/空白，不据此改写构建产物格式。

## T2/T3 操作与实际记录

仍然只有 T2/T3 两组，详见 [test_plan.md](test_plan.md)。当前 **PA1/P7/STM_ADC/TPAD/RV1 实物连线未确认**，未下载、未改硬件接线、未取得串口或逻辑分析仪实测记录。

1. **T2 接线前置**：断电核对板级手册/丝印，确认 P7 的 STM_ADC 实际通 PA1、TPAD 跳帽状态、RV1 输出范围、VDDA/VSSA 与共地。PA7 是 IMU MOSI，不能当作 P7；未知 RV1-P7 走线不得短接，不向 PA1 施加 5V。
2. 按既有 BOOT0=1+RESET 下载本次 HEX，再 BOOT0=0+RESET 运行；释放下载工具 COM 占用，115200/8N1、无流控，发送 `ADC?` + 实际 LF，观察多条 200ms 报告。无丢窗时相邻报告的 n 增量通常为 192/224 等 32 的倍数，用串口时间戳比较较长时间段的 Δn/Δt 是否大致 1000 样本/秒，不能由此宣称精密定时测量。
3. 每次改变接线先断电，分别做已确认的 GND、安全中间电平、板上 3.3V 高参考，记录 raw/avg 范围 0..4095、低中高排序、n 持续增长和 missed。只有 RV1 范围/走线确认后才做中间电平；否则如实记录覆盖缺口，交用户/总控判断验收充分性。
4. **T3**：保持原 CAN STATUS 和 IMU 10ms 采样，间歇发送 PING/COUNT?/ADC?，观察 CAN、UART 回复及 ADC n 持续，控制默认安全行为无异常；舵机优先无负载，不扩大既有授权，不跑 P3 长测试。

| 组 | 预期 | 实际 | 结论 |
| --- | --- | --- | --- |
| T2 | 接线已确认；低/中/高趋势合理；DMA 持续、n 增长 | 未执行，接线待确认，无实测值 | **PENDING** |
| T3 | CAN/IMU/UART/ADC 短时并发正常 | 未执行，无实测日志 | **PENDING** |

若 n 不增长，优先核对 EXTSEL/EXTTRIG、DMA Ch1 circular/halfword、TIM2 clock/PSC/ARR/CCR2、PWM2/CC2E 与启动顺序；不能改成 TIM2_TRGO 或软件触发掩盖故障。

没有编译阻塞项。P4b 尚不能标记 PASS，P4 尚不能标记 ACCEPTED；待用户返回 T2/T3 真实结果后补记。尚未生成 `doc/P04_HANDOFF.md`，没有进入 P5。
