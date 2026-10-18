# P5b Codex 工作清单（一个主要对话）

> **硬前置：** 先读 P5a_HANDOFF、最新 HEAD、实际 `EnvApp_GetSnapshot` 定义，严禁依赖假设 P5a 已经完成。

1. **读取与差异检查。** 看 `main.c`、`control.c/.h`、`control_policy.c`、`local_follow.c`、`actuator.h`、`can_app.c/.h`、`uart_app.c/.h`、`imu.h/imu_types.h`、`adc_app.h`、`FreeRTOSConfig.h`、`Projects/RTnode.uvprojx`、已完成 P5a。若有用户未提交修改，先报告冲突。
2. **审 OLED 厂商代码。** 阅读 `vendor_inputs/03-0.91OLED显示屏STM32F103ZET6_IIC例程.rar` 中的 `HARDWARE/OLED/*`，确认 128×32、0x3C、ASCII 字库、软件 I²C、RES 依赖及可能的许可限制；决定保留哪些字库/初始化参数，不能直接搬 StdPeriph 整工程。
3. **I²C1 驱动。** PB6 SCL/PB7 SDA 100kHz AF_OD，独立 Handle，保留板载 24C02 原有电路。不读写/擦除 EEPROM。一个 `HAL_I2C_MspInit` 根据 Instance 为 I²C1/I²C2 分派；避免重复同名函数或改坏已通过的 I²C2。
4. **SSD1306。** 确认 7 位地址和 128×32，命令/数据控制字节及 512B 显存；实现最少的文字、数字、清屏、逐页/短块刷新与硬件超时；四针板没有额外硬件 RESET。开机首屏 ENV。
5. **四类数据。** 使用现有 Env/IMU/Control/Actuator/ADC getter；在 CanTask 和 UartTask 现有生产点各新增最少只读快照，禁止 OLED 读取外设 FIFO、DMA 环缓冲或依赖不完整指针。对 stale/invalid 显示占位，避免编造值。
6. **KEY1 PE3。** 从 ControlTask 删除其旧停止职责，DisplayTask 以独立轻量轮询消抖负责四页切换：一次按下只翻一次，松开再触发。
7. **KEY0 PE4。** 优先在 ControlTask 每周期处理；首次 LOCAL 满足 IMU 条件自动授权/捕获参考，保留 priming；按 KEY0 停止并锁定直到 MCU 复位，任何后续 CAN MODE 或 REMOTE 不得 Enable。普通 CAN STOP/Mode 行为保留；**不要**在每次 `P3B_DISABLED` 自动重启；将锁存只读给状态显示。注意 `P3B_OUTPUT_STAGE=0` 默认门控不可擅自放宽。
8. **复核时序与构建。** 把 OLED I²C 传输拆短/分页或降低刷新频率，至少保留 10ms IMU/20ms Control 周期运行；不在 ControlTask 处理 OLED。Keil ARMCC5 编译检查静态栈/链接文件；现有 CAN/UART/ADC 冒烟。
9. **T2 / T3。** 用户使用 CH340、SocketCAN、逻辑分析仪和人工观察，按 `test_plan.md` 记录实际值。不可在缺少实机结果时填 PASS。
10. **交接。** 更新 P5b_HANDOFF 与根 `P05_HANDOFF.md`；确认全部 PASS 后才建议 P5 ACCEPTED，**不进入 P6**。

### STOP 安全验收不可删

(A) 上电时 KEY0 按住不输出；(B) 已启动后短按 KEY0 及时停；(C) 松开不恢复；(D) 新有效 CAN MODE+CONTROL 帧也不能恢复；(E) 只有 MCU 复位才能再次取得自动 LOCAL 启动资格；(F) 普通 CAN STOP 仍按原有协议工作，不因取消 KEY0 启动而偷偷立即自动 LOCAL 重启。
