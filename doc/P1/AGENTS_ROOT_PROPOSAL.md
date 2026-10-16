# RT-Node 根目录 AGENTS.md 内容提案（仅用于 Codex 将来审查和建立，不自动生效）

> 当 Codex 在实际工作区启动时，先检查仓库根目录是否已有 AGENTS.md：如果已存在，按兼容性和最小增量原则合并必要规则，不能覆盖；如果不存在，在用户授权的实施范围内可建立本文件内容的精简版。**本提案存放在 Doc 包内，不能替代根目录真正的 AGENTS.md。**

## 项目约束

- MCU STM32F103ZET6；Keil MDK5 / ARM Compiler 5.06u7 / STM32 HAL；未来 FreeRTOS。必须使用现有 `Projects/RTnode.uvprojx` 作为唯一 Keil 工程，不因其未上传 Git 就生成另一个工程。
- TIM3 full remap：CH1/PC6 Pan，CH2/PC7 Tilt，50 Hz / 1 µs；Pan 限幅 1000–2000 µs，Tilt 1200–1600 µs，双轴中位 1500 µs。不得在未经设计批准时变更。
- PC6/PC7 默认安全禁止，异常路径优先切断 PWM；紧急关闭不得依赖正在运行的 RTOS/通信；普通软件禁用不能宣称工业急停或机械抱闸。
- 未来正常执行器唯一所有者为 ControlTask；其它任务不得写 TIM3。紧急关闭例外必须可审计。
- P1a 不建立 FreeRTOS Scheduler，不实现 IMU/CAN/UART DMA/ADC DMA/BME280/OLED、不迁移旧通信或完整 Gimbal 应用。
- 预留 SPI1 PA5/PA6/PA7、I2C2 PB10/PB11、I2C1 PB6/PB7、CAN1 PA11/PA12、USART1 PA9/PA10、ADC1 PA1。
- 不改旧仓库 VisionArm-MCU。迁移代码记录来源固定 commit、修改原因和回归范围。

## 质量与工作流

- 工作前记录当前 HEAD / status / 用户未提交的本地改动；不强制 reset，不覆写用户文件、不擅自 push/force push。
- 仅读取/修改任务文档明确允许的路径；异常、硬件冲突或需改变冻结安全约束时停止并报告。
- Keil 构建真实运行才能填 PASS；其他工具不可用填 NOT_RUN；不可编造编译、逻辑分析仪、机械测试、周期和资源数据。
- 复用已存在 HAL/CMSIS/Startup/FreeRTOS 实际源码，不无故替换整套库，不无差别加入全部 port 或 heap。
- 处理 IRQ、AFIO_MAPR/SWJ、TIM3 CCER/CEN、GPIO 安全寄存器和 OC preload 时先检查手册及本仓库驱动实现；单元测试不能代替板上验证。
- 交付记录 commit/修改清单/Build Log/MAP/测试状态/风险；重要硬件验收证据必须来自用户实机。
