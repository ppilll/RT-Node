# 根目录 AGENTS.md 最小增量提案（**不要直接覆盖**）

若仓库已有适用 `AGENTS.md`，Codex 先读现有内容，仅经用户明确许可追加不冲突条款；如无文件，这里只是提案，由用户决定是否创建。

- STM32F103ZET6 / Keil MDK5 / ARM Compiler 5.06u7 / HAL / FreeRTOS V10.4.6，唯一 `Projects/RTnode.uvprojx`。
- 冻结 TIM3_CH1→PC6、TIM3_CH2→PC7、AFIO full remap、50Hz、1µs、Pan 1000–2000、Tilt 1200–1600、中位1500。
- 最早 PC6/PC7 safe-low，默认禁用；正常驱动唯一 ControlTask；紧急安全关断可以绕过，但必须硬件先关、故障锁存且异常不返回。绝不声明软件 PWM 关闭=机械制动。
- 控制任务不能等待 UART/I2C/SPI/CAN；任务使用静态 TCB/Stack；不添加错误 FreeRTOS port 或 heap；中断优先级、SVC/PendSV/SysTick 唯一。
- 不侵占 P0 预留引脚，额外测试 pin 需要先核对开发板并获批准。
- 默认固件无自动 actuator 使能；P1b 与 P1a 临时测试宏互斥；任何无载测试固件不能直接带载扫动。
- 编译器、MAP、Flash/SRAM、任务周期/时长、LA 波形和故障路径只写真实证据，未运行标 NOT_RUN。
- 不改旧 VisionArm-MCU，不重置/覆盖本地新测试版，不未经授权 push/提交。

阶段局部细节留在本包，不复制所有测试脚本到 AGENTS.md。
