# P5a Codex 工作清单（一个主要对话）

> 工作区是 RT-Node 实际 Git checkout。本包是规格输入。**只写 P5a 代码**，不可先编码 OLED/KEY0/KEY1。

1. **读取真实基线。** 查看当前 HEAD、`git status`、`main.c`、`hal_conf.h`、`FreeRTOSConfig.h`、`Projects/RTnode.uvprojx`、`uart_app.c`、`imu.c`；检测有无并行未提交修改，发现冲突先报告，不覆盖用户工作。用户提供 P4 参考 SHA `4093ca3...`，不是强制回退目标。禁止反复做整库 hash。
2. **厂商驱动评审。** 阅读 `vendor_inputs/BME280_I2C.ino` 和 Bosch SensorAPI（或已有库），记录可复用点、风险、BSD 源码许可、编译宏与单位。严禁从 BMP280 错当 BME280。
3. **硬件 HAL I²C2。** GPIOB PB10/PB11 AF 开漏、100kHz、独立静态 I2C Handle；不要更改 SPI1/IMU 初始化。添加 `stm32f1xx_hal_i2c.c` 到 Keil 工程（先确认实际工程中是否已经存在）。只使用适当 HAL callbacks，避免与未来 I²C1 MSP 冲突。
4. **BME280 驱动。** 初始化 I²C2、检查 ACK/ID=0x60、读取校准参数、设置 T/H/P x1 与 Forced mode、定时轮询采集；实现 HAL 读写和有界等待/超时。正常 I²C 失败只报告 ENV-local invalid。不要在大段时间里关 IRQ。
5. **快照与任务。** 静态 SensorTask，~500ms 周期；发布完整 `EnvSnapshot`。保留精确成功 tick 并处理失效；不要动 ControlTask 等优先级。
6. **UART。** 只在 UartTask 的 `ParseByte`/reply 层增加 `ENV?`，不破坏 DMA RX/TX、ADC/COUNT/PING；计算字符串边界，整数字段格式化可以复用已有辅助函数。
7. **编译和快速检查。** Keil/ARMCC5 编译，不存在编译环境应注明 NOT RUN；检查链接所有新文件、静态资源消耗、符号冲突和无 printf 大栈增长。可选最小的主机补偿用例，不建测试矩阵。
8. **交付。** 只记录实质文件列表、关键变更、Bosch 版本/许可/单位、编译状态、T1 指令与实际观察；在用户完成 T1 后填写 `P5a_HANDOFF.md`。实机数据未经用户提供不得写 PASS。完成后等待下一次 P5b Codex 对话。

### 变更上限

只改环境驱动、HAL I²C Keil 工程注册、主初始化、UART `ENV?` 相关位置；如需超出，先简述理由与影响。禁改 `P3B_OUTPUT_STAGE`、任何 PWM/KEY/CAN/IMU 代码。
